// ProsperoStore - A damaged trust record cannot reset rollback protection.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "catalog/client.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>

unsigned requests = 0;
namespace store::net
{
Response fetch(const std::string &, Purpose, std::size_t, std::string &, Control &,
               const std::string &)
{
    ++requests;
    Response result;
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
    // One request to the catalog's site and one to its mirror.
    assert(!memory.refresh(snapshot, control, error) && error == "offline" && requests == 2);
    requests = 0;
    assert(!client.refresh(snapshot, control, error) && requests == 2);
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
    fs::remove_all(path);
}
