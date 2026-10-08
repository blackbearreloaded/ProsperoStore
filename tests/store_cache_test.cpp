// ProsperoStore - A damaged trust record cannot reset rollback protection.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog/client.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>

unsigned requests = 0;
std::map<std::string, std::string> remote;
std::vector<std::string> urls;
namespace store::net
{
Response fetch(const std::string &url, Purpose, std::size_t limit, std::string &body, Control &,
               const std::string &)
{
    ++requests;
    urls.push_back(url);
    Response result;
    if (remote.contains(url))
    {
        body = remote.at(url);
        assert(body.size() <= limit);
        result.status = 200;
        return result;
    }
    result.error = "offline";
    return result;
}
} // namespace store::net
int main()
{
    char path[] = "/tmp/prosperostore-cache-XXXXXX";
    assert(mkdtemp(path));
    namespace fs = std::filesystem;
    const auto record = fs::path(path) / "current";
    store::catalog::Client client(path);
    store::catalog::Snapshot snapshot;
    store::net::Control control;
    std::string error;
    store::catalog::Client memory("");
    assert(!memory.cached(snapshot, error));
    assert(!memory.refresh(snapshot, control, error) && error == "offline" && requests == 1);
    requests = 0;
    assert(!client.refresh(snapshot, control, error) && requests == 1);
    requests = 0;
    fs::create_directory(record);
    assert(!client.refresh(snapshot, control, error) && requests == 0);
    fs::remove(record);
    std::ofstream(record) << std::string(store::catalog::kVersionsLimit + 65, 'x');
    assert(!client.refresh(snapshot, control, error) && requests == 0);
    fs::remove(record);
    fs::create_symlink("absent", record);
    assert(!client.refresh(snapshot, control, error) && requests == 0);
    fs::remove(record);
    std::ofstream(record) << "damaged";
    assert(!client.refresh(snapshot, control, error) && requests == 0);

    // A schema-compatible development feed needs no signature file when opted out.
    using namespace store::catalog;
    const std::string base = "https://dev.example/api/v1/";
    const std::string index = R"({"schema":3,"apps":[]})";
    const std::string versions = R"({"schema":3,"apps":{}})";
    const std::string detail =
        R"({"schema":3,"titleid":"PPSA99500","name":"Dev app","kind":"app","status":"coming_soon"})";
    const std::string manifest = "{\"schema\":3,\"sequence\":1,\"commit\":\"dev\",\"files\":{" +
                                 std::string("\"index.json\":\"") + sha256(index) +
                                 "\",\"versions.json\":\"" + sha256(versions) +
                                 "\",\"apps/PPSA99500.json\":\"" + sha256(detail) + "\"}}";
    remote = {{base + "manifest.json", manifest},
              {base + "index.json", index},
              {base + "versions.json", versions},
              {base + "apps/PPSA99500.json", detail}};
    Client development(path, base, false);
    urls.clear();
    Snapshot custom;
    assert(development.refresh(custom, control, error));
    assert(custom.accepted && custom.online && !custom.verified);
    assert((urls == std::vector<std::string>{base + "manifest.json", base + "index.json",
                                             base + "versions.json"}));
    Entry app;
    assert(development.detail(custom, "PPSA99500", app, control, error));
    assert(app.name == "Dev app");
    remote.clear();
    Snapshot offline;
    assert(development.cached(offline, error) && offline.accepted && !offline.verified);
    assert(development.detail(offline, "PPSA99500", app, control, error));
    Snapshot other;
    Client different(path, "https://other.example/api/v1/", false);
    assert(!different.cached(other, error));
    Client signed_feed(path, base);
    assert(!signed_feed.cached(other, error)); // Never consumes the unchecked cache.
    assert(!signed_feed.detail(custom, "PPSA99500", app, control, error));
    remote = {{base + "manifest.json", manifest}};
    assert(!signed_feed.refresh(other, control, error)); // Missing signature fails closed.
    remote[base + "manifest.sig"] = std::string(64, 'x');
    assert(!signed_feed.refresh(other, control, error)); // Bad signature also fails closed.
    remote[base + "index.json"] = index + " ";
    remote[base + "versions.json"] = versions;
    Client uncached("", base, false);
    assert(!uncached.refresh(other, control, error)); // Hash checks survive the opt-out.
    assert(!other.accepted);
    remote[base + "manifest.json"] = "{}";
    assert(!uncached.refresh(other, control, error)); // Still require the API manifest schema.
    {
        // A mirror: the catalog's own address is down, the second one has the same files.
        const std::string down = "https://down.example/api/v1/";
        remote = {{base + "manifest.json", manifest},
                  {base + "index.json", index},
                  {base + "versions.json", versions},
                  {base + "apps/PPSA99500.json", detail}};
        Client mirrored("", down, false, {"https://also-down.example/api/v1/", base});
        urls.clear();
        Snapshot served;
        assert(mirrored.refresh(served, control, error) && error.empty());
        assert(served.accepted && mirrored.mirrored() && mirrored.active() == base &&
               mirrored.api() == down);
        // Each address is asked once, in order, and the files follow the one that answered.
        assert((urls == std::vector<std::string>{down + "manifest.json",
                                                 "https://also-down.example/api/v1/manifest.json",
                                                 base + "manifest.json", base + "index.json",
                                                 base + "versions.json"}));
        urls.clear();
        assert(mirrored.detail(served, "PPSA99500", app, control, error) &&
               urls == std::vector<std::string>{base + "apps/PPSA99500.json"});
        // The address itself answering again takes over at the next refresh.
        for (const char *name : {"manifest.json", "index.json", "versions.json"})
            remote[down + name] = remote.at(base + name);
        assert(mirrored.refresh(served, control, error) && !mirrored.mirrored());
        // All down: the failure is the first address's, and nothing changes.
        remote.clear();
        assert(!mirrored.refresh(served, control, error) && error == "offline" &&
               !mirrored.mirrored());
        // An icon written for the catalog's address, as the mirror's copy of it.
        assert(rebased(down + "icons/PPSA99500-256.png?v=1", down, base) ==
               base + "icons/PPSA99500-256.png?v=1");
        assert(rebased("https://elsewhere.example/a.png", down, base) ==
               "https://elsewhere.example/a.png");
        assert(official_mirrors().empty()); // none configured yet
    }
    // Clean the source-specific sibling caches as well as the original test directory.
    for (const auto mode : {"\nunsigned", "\nsigned"})
        fs::remove_all(std::string(path) + "-" + sha256(base + mode));
    fs::remove_all(std::string(path) + "-" + sha256("https://other.example/api/v1/\nunsigned"));
    fs::remove_all(path);
}
