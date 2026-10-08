// ProsperoStore - ShadowMount scan policy and safe store work locations.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace store::system
{
struct ScanPolicy
{
    std::vector<std::string> roots;
    std::vector<std::string> manual;
    unsigned depth = 1;
    // Lines of the configuration that name a path the store can't read as one (kept for
    // the debug log). They are left out; the rest of the configuration still counts.
    std::vector<std::string> ignored;
    // Scan paths were set, none could be read, and the built-in ones were used instead.
    bool fell_back = false;
};
// ShadowMountPlus 1.7 at f0d15ffc: manual entries are titles/images, not scan roots.
bool scan_policy(std::string_view config, std::string_view manual, ScanPolicy &out,
                 std::string &error);
bool clean_absolute_path(std::string_view path);
std::string drive_root(std::string_view path);
// The transaction's app folder must be beyond every configured scan depth.
bool work_path_unscanned(const ScanPolicy &policy, std::string_view app_path);
// Why not, for messages: the scan path or manual.lst entry in the way ("" when unscanned).
std::string work_path_conflict(const ScanPolicy &policy, std::string_view app_path);
} // namespace store::system
