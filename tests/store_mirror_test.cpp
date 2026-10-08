// ProsperoStore - The catalog's mirror is used when its own site can't be, and only as far as it
// verifies. Copyright (C) 2026 BlackBearReloaded SPDX-License-Identifier: GPL-3.0-or-later
//
// The fixture is a real, signed copy of the mirror (manifest, signature, index, versions), so the
// whole path runs against the catalog's own keys. The network is a table in this file.
#include "catalog/client.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <unistd.h>

namespace
{
enum class Site
{
    working,
    unreachable,
    block_page // answers 200 with something that isn't the catalog
};
Site main_site = Site::unreachable;
Site mirror_site = Site::working;
std::map<std::string, unsigned> asked;

std::string fixture(const std::string &name)
{
    std::ifstream file(std::string(STORE_TEST_FIXTURES) + "/" + name, std::ios::binary);
    if (!file)
        return {};
    std::ostringstream bytes;
    bytes << file.rdbuf();
    return bytes.str();
}
} // namespace

namespace store::net
{
Response fetch(const std::string &url, Purpose purpose, std::size_t limit, std::string &body,
               Control &, const std::string &)
{
    assert(purpose == Purpose::catalog && catalog::api_url(url));
    const std::string main = std::string(catalog::kApi);
    const std::string mirror = std::string(catalog::kApiMirror) + "api/v1/";
    const bool from_main = url.starts_with(main);
    assert(from_main || url.starts_with(mirror));
    ++asked[from_main ? "main" : "mirror"];
    const Site site = from_main ? main_site : mirror_site;
    Response result;
    if (site == Site::unreachable)
    {
        result.error = from_main ? "main site unreachable" : "mirror unreachable";
        return result;
    }
    // The fixture holds the mirror's files; the main site has nothing here that would verify.
    body = site == Site::block_page || from_main ? std::string("<html>This site is blocked</html>")
                                                 : fixture(url.substr(mirror.size()));
    if (body.empty())
    {
        result.status = 404;
        result.error = "not found";
        return result;
    }
    result.status = 200;
    assert(body.size() <= limit || site == Site::block_page);
    return result;
}
} // namespace store::net

int main()
{
    using namespace store::catalog;
    namespace fs = std::filesystem;
    char path[] = "/tmp/prosperostore-mirror-XXXXXX";
    assert(mkdtemp(path));
    store::net::Control control;
    std::string error;

    // The main site can't be reached: the mirror's signed catalog is loaded instead.
    {
        Client client(path);
        Snapshot snapshot;
        asked.clear();
        assert(client.refresh(snapshot, control, error));
        assert(snapshot.verified && snapshot.online && !snapshot.entries.empty());
        assert(asked["main"] == 1 && asked["mirror"] >= 4);
        // What the mirror lists is fetched from the mirror: its icons name it.
        bool icons = false;
        for (const auto &entry : snapshot.entries)
            if (!entry.icon.empty())
            {
                assert(entry.icon.starts_with(kApiMirror));
                icons = true;
            }
        assert(icons);
        // Later files are asked of the place that worked, not of the main site again.
        asked.clear();
        Entry detail;
        assert(client.detail(snapshot, "PPSA99000", detail, control, error));
        assert(detail.id == "PPSA99000" && detail.large_icon.starts_with(kApiMirror));
        assert(asked["mirror"] == 1 && asked["main"] == 0);
        // A file neither place has: both are asked, and the first failure is the one reported.
        asked.clear();
        assert(!client.detail(snapshot, "PPSA99001", detail, control, error));
        assert(asked["mirror"] == 1 && asked["main"] == 1 && error == "not found");

        // An older catalog than one already accepted is refused from either place.
        Snapshot newer = snapshot;
        newer.manifest.sequence = snapshot.manifest.sequence + 1;
        Client memory("");
        assert(!memory.refresh(newer, control, error));
    }

    // A block page that answers 200 is not the catalog: the mirror is still used.
    {
        main_site = Site::block_page;
        Client memory("");
        Snapshot snapshot;
        assert(memory.refresh(snapshot, control, error) && snapshot.verified);
    }

    // Neither place works: the failure reported is the main site's, and nothing is replaced.
    {
        main_site = Site::unreachable;
        mirror_site = Site::unreachable;
        Client memory("");
        Snapshot snapshot;
        asked.clear();
        assert(!memory.refresh(snapshot, control, error));
        assert(error == "main site unreachable" && asked["main"] == 1 && asked["mirror"] == 1);
        assert(!snapshot.verified);
    }

    // The mirror serving a block page is refused like any unsigned answer.
    {
        mirror_site = Site::block_page;
        Client memory("");
        Snapshot snapshot;
        assert(!memory.refresh(snapshot, control, error) && !snapshot.verified);
    }

    // After a restart the saved catalog still opens, without the network.
    {
        Client client(path);
        Snapshot snapshot;
        assert(client.cached(snapshot, error) && snapshot.verified && !snapshot.online);
    }
    fs::remove_all(path);
    std::cout << "Catalog mirror checks passed\n";
}
