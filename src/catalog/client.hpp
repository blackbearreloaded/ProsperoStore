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
    bool online = false;
};

class Client
{
  public:
    // Empty cache means read-only, in-memory browsing with no filesystem access.
    explicit Client(std::string cache) : cache_(std::move(cache))
    {
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
    std::string cache_;
    // The place that last answered with files that verified: asked first for the next file.
    std::size_t origin_ = 0;
};
} // namespace store::catalog
