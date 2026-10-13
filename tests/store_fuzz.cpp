// ProsperoStore - Fuzz target for everything the installer parses: ZIP and JSON.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog/catalog.hpp"
#include "install/archive.hpp"
#include "install/files.hpp"
#include "social/comments.hpp"
#include <atomic>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace
{
const std::string &scratch()
{
    static const std::string path = "/tmp/prospero-fuzz-" + std::to_string(getpid());
    return path;
}
void archive(std::string_view bytes)
{
    const auto file = scratch() + ".zip", folder = scratch() + ".out";
    const int descriptor = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0)
        __builtin_trap();
    const auto count = write(descriptor, bytes.data(), bytes.size());
    close(descriptor);
    if (count != static_cast<ssize_t>(bytes.size()))
        __builtin_trap();
    store::install::ArchiveInfo info;
    std::string error;
    std::atomic<bool> cancelled{false};
    std::atomic<std::uint64_t> written{0};
    if (store::install::inspect_archive(file, "PPSA99500", info, error))
    {
        const bool unpacked =
            store::install::extract_archive(file, "PPSA99500", folder, cancelled, written, error);
        // Never more than the directory declared, and exactly that on success.
        if (written > info.unpacked || (unpacked && written != info.unpacked))
            __builtin_trap();
    }
    if (!store::install::remove_tree(folder))
        __builtin_trap();
}
void records(std::string_view body)
{
    using namespace store::catalog;
    std::string error;
    Entry entry;
    Receipt receipt;
    Journal journal;
    std::vector<Entry> entries;
    std::map<std::string, std::string> versions;
    parse_index(body, entries, error);
    parse_detail(body, "PPSA99500", entry, error);
    parse_versions(body, versions, error);
    parse_installed(body, entry, error);
    // What a record parser accepts must survive being written out again.
    if (parse_receipt(body, receipt, error))
    {
        Receipt again;
        if (!parse_receipt(format_receipt(receipt), again, error) ||
            again.location != receipt.location || again.release_tag != receipt.release_tag)
            __builtin_trap();
    }
    if (parse_journal(body, journal, error))
    {
        Journal again;
        if (!parse_journal(format_journal(journal), again, error) ||
            again.location != journal.location || again.state != journal.state)
            __builtin_trap();
    }
    // Likes and comments: whatever is kept is bounded, plain, and carries no other address.
    const auto bounded = [](const std::string &text, std::size_t limit)
    {
        std::size_t characters = 0;
        for (const char c : text)
        {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < 32 || byte == 127)
                __builtin_trap();
            characters += (byte & 0xc0) != 0x80;
        }
        if (characters > limit || store::social::plain(text, limit) != text)
            __builtin_trap();
    };
    const auto sane = [](const store::social::Counts &counts)
    {
        if (counts.likes > store::social::kMost || counts.comments > store::social::kMost ||
            (!counts.url.empty() && !store::social::discussion_url(counts.url)))
            __builtin_trap();
    };
    store::social::Index likes;
    if (store::social::parse_index(body, likes))
        for (const auto &[id, counts] : likes)
        {
            if (!title_id(id))
                __builtin_trap();
            sane(counts);
        }
    store::social::Thread thread;
    if (store::social::parse_thread(body, "PPSA99500", thread))
    {
        sane(thread.counts);
        if (thread.latest.size() > store::social::kComments)
            __builtin_trap();
        for (const auto &comment : thread.latest)
        {
            bounded(comment.author, store::social::kAuthorLimit);
            bounded(comment.text, store::social::kTextLimit);
            if (comment.text.empty() || (!comment.date.empty() && comment.date.size() != 10))
                __builtin_trap();
        }
    }
    bounded(store::social::plain(body, store::social::kTextLimit), store::social::kTextLimit);
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    if (size == 0)
        return 0;
    const std::string_view body(reinterpret_cast<const char *>(data) + 1, size - 1);
    if (data[0] & 1)
        archive(body);
    else
        records(body);
    return 0;
}
