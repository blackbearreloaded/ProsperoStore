// ProsperoStore - Verified catalog models and trust boundary.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace store::catalog
{
constexpr std::size_t kIndexLimit = 4 * 1024 * 1024;
constexpr std::size_t kDetailLimit = 64 * 1024;
constexpr std::size_t kVersionsLimit = 1024 * 1024;
constexpr std::uint64_t kArtifactLimit = 2ULL * 1024 * 1024 * 1024;

bool title_id(std::string_view value);
bool version(std::string_view value);
bool update_available(std::string_view installed, std::string_view available);
bool api_url(std::string_view url);
inline constexpr const char *kDefaultApi = "https://homebrew.page/api/v1/";
// Other places that publish the same official catalog, tried in this order when
// homebrew.page can't be reached. The catalog's signature and file hashes are checked
// exactly as for homebrew.page, so a mirror can serve it but never change it.
// None yet: add each as a full API directory, e.g. "https://mirror.example/api/v1/".
inline const std::vector<std::string> &official_mirrors()
{
    static const std::vector<std::string> mirrors{};
    return mirrors;
}
// A catalog address (an icon, say) written for one API directory, as the same file in
// another. An address outside `from` is returned as it is.
inline std::string rebased(std::string_view url, std::string_view from, std::string_view to)
{
    return from != to && url.starts_with(from)
               ? std::string(to) + std::string(url.substr(from.size()))
               : std::string(url);
}
// Empty input restores the official API; otherwise require an HTTPS directory URL.
bool normalize_api(std::string_view value, std::string &out);
bool artifact_url(std::string_view url, bool redirected = false);
std::string sha256(std::string_view bytes);
bool hex_bytes(std::string_view text, std::span<std::uint8_t> bytes);

struct Entry
{
    std::string id, name, author, kind, status;
    std::string version, content_version, format, icon, icon_hash, released, updated, large_icon;
    std::string description, license, source, page, artifact, digest, release_notes;
    std::uint64_t size = 0;
    // Not from the catalog's files: set by the store on entries that come from the custom
    // catalog shown beside the official one.
    bool extra = false;
    // What the catalog's scan of the release file found. Advice, not a guarantee: the scan reads
    // the file and can miss things. Empty strings mean "not known", never "stays".
    std::string sandbox;       // "stays", "leaves", "unclear"
    std::string build;         // "attested", "workflow", "developer"
    std::uint32_t helpers = 0; // helper programs that run outside the sandbox
    std::uint32_t helpers_unapproved = 0;
};

struct Manifest
{
    std::uint64_t sequence = 0;
    std::string commit;
    std::map<std::string, std::string> files;
    bool verifies(std::string_view path, std::string_view body) const;
};

struct Receipt
{
    std::string id, location, content_version, release_tag, digest, installed_at;
};

// The one transaction in progress. content_version is the version being put in
// place (empty until it has been read from the unpacked app).
struct Journal
{
    std::string operation, state, id, location, content_version, release_tag, digest;
};

bool parse_installed(std::string_view body, Entry &out, std::string &error);
bool parse_receipt(std::string_view body, Receipt &out, std::string &error);
std::string format_receipt(const Receipt &receipt);
bool parse_journal(std::string_view body, Journal &out, std::string &error);
std::string format_journal(const Journal &journal);

using PublicKey = std::array<std::uint8_t, 32>;
std::array<PublicKey, 2> public_keys();
bool verify_manifest(std::string_view body, std::string_view signature, std::uint64_t highest,
                     std::span<const PublicKey> keys, Manifest &out, std::string &error);
bool parse_manifest(std::string_view body, std::uint64_t highest, Manifest &out,
                    std::string &error);
bool parse_index(std::string_view body, std::vector<Entry> &out, std::string &error);
bool parse_detail(std::string_view body, std::string_view expected, Entry &out, std::string &error);
bool parse_versions(std::string_view body, std::map<std::string, std::string> &out,
                    std::string &error);
} // namespace store::catalog
