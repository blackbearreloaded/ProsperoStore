// ProsperoStore - Verified catalog refresh and content-addressed offline cache.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "catalog/catalog.hpp"
#include "net/http.hpp"

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

class Client
{
  public:
    // Empty cache means read-only, in-memory browsing with no filesystem access.
    explicit Client(std::string cache, std::string api = kDefaultApi, bool verify = true)
        : cache_(std::move(cache)), api_(std::move(api)), verify_(verify)
    {
        // Keep the official signed cache and its rollback record across upgrades.
        if (!cache_.empty() && (api_ != kDefaultApi || !verify_))
            cache_ += "-" + sha256(api_ + (verify_ ? "\nsigned" : "\nunsigned"));
    }
    bool cached(Snapshot &out, std::string &error);
    bool refresh(Snapshot &out, net::Control &control, std::string &error);
    bool detail(const Snapshot &snapshot, const std::string &id, Entry &out, net::Control &control,
                std::string &error);

  private:
    bool file(const Manifest &manifest, const std::string &name, std::size_t limit,
              std::string &body, net::Control *control, std::string &error);
    bool parse(const std::string &bundle, std::uint64_t highest, Snapshot &out,
               net::Control *control, std::string &error);
    bool manifest(const std::string &bundle, std::uint64_t highest, Manifest &out,
                  std::string &error) const;
    std::string cache_;
    std::string api_;
    bool verify_ = true;
};
} // namespace store::catalog
