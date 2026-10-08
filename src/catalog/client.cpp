// ProsperoStore - Verify before parsing and publish only a complete catalog generation.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <set>
#include "catalog/client.hpp"
#include "core/save_file.hpp"
#include <fcntl.h>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

namespace store::catalog
{
namespace
{
// open with O_NOFOLLOW also works where the sandbox prohibits lstat. Keep the
// descriptor through validation and read so a path swap cannot replace it.
int read_trust(const std::string &path, std::string &out)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return errno == ENOENT ? 0 : -1;
    struct stat info
    {
    };
    bool valid = ::fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 64 &&
                 static_cast<std::uint64_t>(info.st_size) <= kVersionsLimit + 64;
    std::string candidate;
    char bytes[4096];
    while (valid)
    {
        const auto count = ::read(fd, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
        {
            valid = false;
            break;
        }
        if (count == 0)
            break;
        if (candidate.size() + static_cast<std::size_t>(count) > kVersionsLimit + 64)
        {
            valid = false;
            break;
        }
        candidate.append(bytes, static_cast<std::size_t>(count));
    }
    if (::close(fd) != 0)
        valid = false;
    if (!valid)
        return -1;
    out = std::move(candidate);
    return 1;
}
bool flush_directory(const std::string &path)
{
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0)
        return false;
    const bool ok = ::fsync(descriptor) == 0;
    return ::close(descriptor) == 0 && ok;
}
} // namespace

bool Client::file(const Manifest &manifest, const std::string &name, std::size_t limit,
                  std::string &body, net::Control *control, std::string &error)
{
    const auto found = manifest.files.find(name);
    if (found == manifest.files.end())
    {
        error = "The app is no longer listed";
        return false;
    }
    const std::string path = cache_ + "/" + found->second;
    if (!cache_.empty() && hui::save::read_file(path, &body, limit) &&
        manifest.verifies(name, body))
        return true;
    if (!control)
    {
        error = "The offline catalog is incomplete";
        return false;
    }
    const auto response = net::fetch(api_ + name, net::Purpose::catalog, limit, body, *control);
    if (!response.ok())
    {
        error = response.error;
        return false;
    }
    if (!manifest.verifies(name, body))
    {
        error = "The catalog file could not be verified";
        return false;
    }
    error = cache_.empty() ? std::string{} : hui::save::write_atomic(path, body);
    return error.empty();
}

bool Client::manifest(const std::string &bundle, std::uint64_t highest, Manifest &out,
                      std::string &error) const
{
    if (bundle.size() <= 64)
    {
        error = "No verified offline catalog";
        return false;
    }
    const auto body = std::string_view(bundle).substr(64);
    return verify_ ? verify_manifest(body, std::string_view(bundle).substr(0, 64), highest,
                                     public_keys(), out, error)
                   : parse_manifest(body, 0, out, error);
}

bool Client::parse(const std::string &bundle, std::uint64_t highest, Snapshot &out,
                   net::Control *control, std::string &error)
{
    Snapshot next;
    if (!manifest(bundle, highest, next.manifest, error))
        return false;
    std::string index, versions;
    if (!file(next.manifest, "index.json", kIndexLimit, index, control, error) ||
        !file(next.manifest, "versions.json", kVersionsLimit, versions, control, error) ||
        !parse_index(index, next.entries, error) || !parse_versions(versions, next.versions, error))
        return false;
    next.verified = verify_;
    next.accepted = true;
    next.online = control != nullptr;
    out = std::move(next);
    return true;
}

bool Client::cached(Snapshot &out, std::string &error)
{
    std::string bundle;
    if (cache_.empty() || read_trust(cache_ + "/current", bundle) != 1)
    {
        error = "No verified offline catalog";
        return false;
    }
    return parse(bundle, out.manifest.sequence, out, nullptr, error);
}

bool Client::refresh(Snapshot &out, net::Control &control, std::string &error)
{
    std::string normalized;
    if (!normalize_api(api_, normalized) || normalized != api_)
    {
        error = "The catalog API URL is invalid";
        return false;
    }
    if (!cache_.empty() && !hui::save::ensure_directory(cache_))
    {
        error = "The catalog cache is unavailable";
        return false;
    }
    // Recover the persisted high-water mark even if one cached data file is damaged.
    std::uint64_t highest = out.manifest.sequence;
    std::string current;
    const std::string trust_path = cache_ + "/current";
    const int trust_state = cache_.empty() ? 0 : read_trust(trust_path, current);
    if (trust_state < 0)
    {
        error = "The saved catalog trust record is inaccessible or damaged";
        return false;
    }
    if (trust_state == 1)
    {
        Manifest previous;
        std::string ignored;
        if (!manifest(current, highest, previous, ignored))
        {
            error = "The saved catalog trust record is damaged";
            return false;
        }
        highest = previous.sequence;
    }
    std::string body, signature;
    auto response =
        net::fetch(api_ + "manifest.json", net::Purpose::catalog, kVersionsLimit, body, control);
    if (!response.ok())
    {
        error = response.error;
        return false;
    }
    if (verify_)
    {
        response = net::fetch(api_ + "manifest.sig", net::Purpose::catalog, 64, signature, control);
        if (!response.ok())
        {
            error = response.error;
            return false;
        }
    }
    else
        signature.assign(64, '\0'); // Same bundle layout, isolated from every signed cache.
    Snapshot next;
    const std::string bundle = signature + body;
    if (!parse(bundle, highest, next, &control, error))
        return false;
    if (cache_.empty())
    {
        out = std::move(next);
        return true;
    }
    // Data files are durable before the atomic pointer to the generation changes.
    if (!flush_directory(cache_))
    {
        error = "The catalog cache could not be committed";
        return false;
    }
    error = hui::save::write_atomic(cache_ + "/current", bundle);
    if (!error.empty() || !flush_directory(cache_))
    {
        error = "The catalog trust record could not be committed";
        return false;
    }
    out = std::move(next);
    return true;
}

bool Client::detail(const Snapshot &snapshot, const std::string &id, Entry &out,
                    net::Control &control, std::string &error)
{
    if (!snapshot.accepted || (verify_ && !snapshot.verified) || !title_id(id))
    {
        error = "The catalog is not verified";
        return false;
    }
    std::string body;
    return file(snapshot.manifest, "apps/" + id + ".json", kDetailLimit, body, &control, error) &&
           parse_detail(body, id, out, error);
}
} // namespace store::catalog
