// ProsperoStore - Verified catalog refresh and content-addressed offline cache.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "catalog/catalog.hpp"
#include "net/http.hpp"
#include <set>

namespace store::catalog
{
struct Snapshot
{
    Manifest manifest;
    std::vector<Entry> entries;
    std::map<std::string, std::string> versions;
    bool verified = false;
    bool accepted = false; // complete and valid under the user's selected trust policy
    bool online = false;
};

// The official catalog with a custom one beside it. The custom catalog only adds apps:
// where both list a title ID the official entry stays, so a custom catalog can never
// stand in for an app the official one offers. clashes names the entries left out.
inline Snapshot with_extra(Snapshot official, const Snapshot &extra,
                           std::vector<std::string> *clashes)
{
    if (!extra.accepted)
        return official;
    std::set<std::string> listed;
    for (const auto &entry : official.entries)
        listed.insert(entry.id);
    for (auto entry : extra.entries)
    {
        if (listed.contains(entry.id))
        {
            if (clashes)
                clashes->push_back(entry.id);
            continue;
        }
        entry.extra = true;
        if (const auto version = extra.versions.find(entry.id); version != extra.versions.end())
            official.versions[entry.id] = version->second;
        official.entries.push_back(std::move(entry));
    }
    return official;
}

class Client
{
  public:
    // Empty cache means read-only, in-memory browsing with no filesystem access.
    // mirrors: other API directories publishing the same catalog. A refresh tries api
    // first, then each mirror, and uses the first that loads and passes every check.
    explicit Client(std::string cache, std::string api = kDefaultApi, bool verify = true,
                    std::vector<std::string> mirrors = {})
        : cache_(std::move(cache)), api_(std::move(api)), verify_(verify),
          mirrors_(std::move(mirrors)), active_(api_)
    {
        // Keep the official signed cache and its rollback record across upgrades.
        if (!cache_.empty() && (api_ != kDefaultApi || !verify_))
            cache_ += "-" + sha256(api_ + (verify_ ? "\nsigned" : "\nunsigned"));
    }
    bool cached(Snapshot &out, std::string &error);
    bool refresh(Snapshot &out, net::Control &control, std::string &error);
    bool detail(const Snapshot &snapshot, const std::string &id, Entry &out, net::Control &control,
                std::string &error);
    // The API directory that served the last successful refresh (api until one succeeds).
    const std::string &active() const
    {
        return active_;
    }
    const std::string &api() const
    {
        return api_;
    }
    bool mirrored() const
    {
        return active_ != api_;
    }

  private:
    bool file(const Manifest &manifest, const std::string &name, std::size_t limit,
              std::string &body, net::Control *control, std::string &error);
    bool parse(const std::string &bundle, std::uint64_t highest, Snapshot &out,
               net::Control *control, std::string &error);
    bool manifest(const std::string &bundle, std::uint64_t highest, Manifest &out,
                  std::string &error) const;
    bool refresh_from(const std::string &base, std::uint64_t highest, Snapshot &next,
                      std::string &bundle, net::Control &control, std::string &error);
    std::string cache_;
    std::string api_;
    bool verify_ = true;
    std::vector<std::string> mirrors_;
    std::string active_; // where files are fetched from
};
} // namespace store::catalog
