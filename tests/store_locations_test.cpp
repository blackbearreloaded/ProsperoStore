// ProsperoStore - Scan overrides must never expose staging as an installed app.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "system/locations.hpp"
#include <algorithm>
#include <cassert>

int main()
{
    using namespace store::system;
    ScanPolicy policy;
    std::string error;
    assert(scan_policy("", "", policy, error));
    assert(policy.roots.size() == 34 && policy.depth == 1);
    assert(work_path_unscanned(policy, "/mnt/ext1/prosperostore/staging/PPSA99001"));
    assert(!work_path_unscanned(policy, "/mnt/ext1/PPSA99001"));
    assert(scan_policy("SCANPATH = /mnt/ext1/homebrew/ ; note\nscanpath=/data/homebrew\n"
                       "recursive_scan=YES\nscan_depth=1\n",
                       "# manual titles\n/data/custom/PPSA99002\n", policy, error));
    assert(policy.roots.size() == 4 && policy.depth == 2 && policy.manual.size() == 1);
    assert(std::find(policy.roots.begin(), policy.roots.end(), "/data/custom") ==
           policy.roots.end());
    assert(!work_path_unscanned(policy, "/data/custom/PPSA99002/nested"));
    assert(work_path_unscanned(policy, "/data/prosperostore/staging/PPSA99002"));
    // A stray "/" or drive line in manual.lst is no scanned place; a game folder still is.
    assert(scan_policy("", "/\n/data\n/mnt/usb0\n/data/custom/PPSA99002\n", policy, error));
    assert(policy.manual.size() == 4);
    assert(work_path_unscanned(policy, "/data/prosperostore/staging/PPSA99002"));
    assert(work_path_unscanned(policy, "/mnt/usb0/prosperostore/staging/PPSA99002"));
    assert(!work_path_unscanned(policy, "/data/custom/PPSA99002/nested"));
    assert(scan_policy("scanpath=/data/prosperostore\nscan_depth=2", "", policy, error));
    assert(!work_path_unscanned(policy, "/data/prosperostore/staging/PPSA99002"));
    assert(scan_policy("scanpath=/data/../system", "", policy, error) && policy.fell_back);
    assert(std::find(policy.roots.begin(), policy.roots.end(), "/system") == policy.roots.end());
    // A doubled slash or quotes are tidied; a line that still can't be read is left out
    // and remembered, and the rest of the configuration counts.
    assert(scan_policy("scanpath=/data//homebrew", "", policy, error));
    assert(policy.roots.front() == "/data/homebrew" && policy.ignored.empty());
    assert(scan_policy("scanpath=\"/data/homebrew/\"\nscanpath=data/games\nscanpath=/mnt/usb0/apps",
                       "games/PPSA99002\n/data/custom/PPSA99003\n", policy, error));
    assert(std::find(policy.roots.begin(), policy.roots.end(), "/data/homebrew") !=
           policy.roots.end());
    assert(std::find(policy.roots.begin(), policy.roots.end(), "/mnt/usb0/apps") !=
           policy.roots.end());
    assert(policy.ignored.size() == 2 && policy.ignored[0] == "scanpath=data/games" &&
           policy.manual.size() == 1);
    // As a user had it (2026-10-08): the doubled slash switched installing off.
    assert(scan_policy("scanpath=/data/homebrew\nscanpath=/mnt/usb0/data/homebrew\n"
                       "scanpath=/mnt//ext1/data/homebrew\n",
                       "/mnt/usb0/data/homebrew\n/ext1/data/homebrew\n/data/homebrew\n", policy,
                       error));
    assert(policy.roots.size() == 5 && policy.roots[2] == "/mnt/ext1/data/homebrew" &&
           policy.ignored.empty() && policy.manual.size() == 3);
    assert(work_path_unscanned(policy, "/mnt/ext1/prosperostore/staging/PPSA99109"));
    // Custom scan paths and none readable: the built-in folders are used, and it is said.
    assert(scan_policy("scanpath=data/games", "", policy, error) && policy.fell_back &&
           policy.ignored.size() == 1 && policy.roots.size() == 34);
    assert(!scan_policy(std::string(300000, 'x'), "", policy, error));
    assert(drive_root("/mnt/ext1/homebrew") == "/mnt/ext1");
    assert(drive_root("/mnt/ext10/homebrew").empty());
    assert(drive_root("/system/app").empty());
    assert(scan_policy("scan_depth=0x2", "", policy, error) && policy.depth == 2);
    assert(scan_policy("scan_depth=02", "", policy, error) && policy.depth == 2);
    assert(scan_policy("recursive_scan=ro", "", policy, error) && policy.depth == 2);
}
