// ProsperoStore - The store's own folder, taken back when another user left files in it.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "system/store_folder.hpp"
#include "install/worker.hpp"
#include "system/worker_launch.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace store::system
{
namespace
{
// How many entries (the folder included) belong to someone else; at most `limit` looked at.
int foreign(const std::string &path, uid_t owner, int depth, int &limit)
{
    struct stat info
    {
    };
    if (limit <= 0 || lstat(path.c_str(), &info) != 0)
        return 0;
    --limit;
    int found = info.st_uid != owner ? 1 : 0;
    if (!S_ISDIR(info.st_mode) || depth >= 6)
        return found;
    DIR *folder = opendir(path.c_str());
    if (!folder)
        return found + 1; // can't even be listed: certainly not ours to use
    while (const dirent *entry = readdir(folder))
    {
        const std::string name = entry->d_name;
        if (name != "." && name != "..")
            found += foreign(path + "/" + name, owner, depth + 1, limit);
    }
    closedir(folder);
    return found;
}
} // namespace

std::string reclaim_store_folder(const std::string &folder)
{
    int limit = 4000;
    int before = foreign(folder, geteuid(), 0, limit);
#ifdef STORE_DEVELOPMENT
    // A scripted run can ask for the worker pass on a folder that has no need of it.
    struct stat forced
    {
    };
    if (before == 0 && stat((folder + "/dev/reclaim.txt").c_str(), &forced) == 0)
        before = -1;
#endif
    if (before == 0)
        return "nothing to reclaim";
    const int result = install::worker_reclaim(launch_worker, folder);
    limit = 4000;
    const int after = foreign(folder, geteuid(), 0, limit);
    return std::to_string(before) + " entries of another user; worker " +
           (result == 1   ? "reclaimed them"
            : result == 0 ? "failed"
                          : "could not be started") +
           "; " + std::to_string(after) + " left";
}
} // namespace store::system
