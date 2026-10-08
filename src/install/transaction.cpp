// ProsperoStore - Install, update and uninstall as journaled transactions.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef STORE_DEBUG_TRACE
#include "diag/trace.hpp"
#endif
#include "install/transaction.hpp"
#include "core/save_file.hpp"
#include "install/archive.hpp"
#include "install/files.hpp"
#include "system/storage_probe.hpp"
#include "third_party/picosha2/picosha2.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace store::install
{
namespace
{
constexpr std::uint64_t kDownloadMargin = 16ull << 20;
constexpr std::uint64_t kUnpackMargin = 64ull << 20;

struct Paths
{
    std::string work, staging, archive, staged, backups, backup, trashes, trash;
    std::string target, receipts, receipt, journal;
    std::string keeps, keep; // the previous version of each updated app
};

// strict is for starting a transaction: the location must be scanned by
// ShadowMountPlus and the work folders must not be. Recovery and uninstall
// act on what a receipt or the journal already names.
bool resolve(const Environment &environment, const std::string &id, const std::string &location,
             bool strict, Paths &out, std::string &error)
{
    if (environment.root.empty() || !catalog::title_id(id) ||
        !system::clean_absolute_path(location) || location == "/")
    {
        error = "The location isn't available";
        return false;
    }
    const auto &roots = environment.policy.roots;
    if (strict && (std::find(roots.begin(), roots.end(), location) == roots.end() ||
                   location.starts_with("/mnt/shadowmnt")))
    {
        error = "The location isn't one ShadowMountPlus scans";
        return false;
    }
    Paths paths;
    paths.work = environment.work;
    if (paths.work.empty())
    {
        const auto drive = system::drive_root(location);
        if (drive.empty())
        {
            error = "The location's drive isn't supported";
            return false;
        }
        paths.work = drive + "/prosperostore";
    }
    paths.staging = paths.work + "/staging";
    paths.archive = paths.staging + "/" + id + ".zip";
    paths.staged = paths.staging + "/" + id;
    paths.backups = paths.work + "/backup";
    paths.backup = paths.backups + "/" + id;
    paths.trashes = paths.work + "/trash";
    paths.trash = paths.trashes + "/" + id;
    paths.keeps = paths.work + "/previous";
    paths.keep = paths.keeps + "/" + id;
    paths.target = location + "/" + id;
    paths.receipts = environment.root + "/receipts";
    paths.receipt = paths.receipts + "/" + id + ".json";
    paths.journal = environment.root + "/journal.json";
    for (const auto *path : {&paths.staged, &paths.backup, &paths.trash, &paths.keep})
        if (const auto conflict = system::work_path_conflict(environment.policy, *path);
            !conflict.empty())
        {
            // Named, so the ShadowMountPlus setting in the way can be found.
            error = "The store's work folder is inside a scanned location (" + conflict + ")";
            return false;
        }
    out = std::move(paths);
    return true;
}

bool write_durable(const std::string &directory, const std::string &path, std::string_view data)
{
    return hui::save::write_atomic(path, data).empty() && sync_directory(directory);
}

bool clear_journal(const Environment &environment, const Paths &paths)
{
    return (unlink(paths.journal.c_str()) == 0 || errno == ENOENT) &&
           sync_directory(environment.root);
}

bool write_journal(const Environment &environment, const Paths &paths,
                   const catalog::Journal &journal)
{
    return write_durable(environment.root, paths.journal, catalog::format_journal(journal));
}

// The contentVersion of an app folder whose param.json names this title.
bool folder_version(const std::string &folder, const std::string &id, std::string &version)
{
    std::string body, error;
    catalog::Entry metadata;
    if (kind(folder) != Kind::directory || kind(folder + "/sce_sys") != Kind::directory ||
        !read_small(folder + "/sce_sys/param.json", catalog::kDetailLimit, body) ||
        !catalog::parse_installed(body, metadata, error) || metadata.id != id)
        return false;
    version = metadata.content_version;
    return true;
}

// Ownership, revalidated from disk: a receipt for this title, this location
// and the version that is actually there.
bool managed(const Paths &paths, const std::string &id, const std::string &location,
             std::string &version)
{
    std::string body, error, installed;
    catalog::Receipt receipt;
    if (!read_small(paths.receipt, 16 * 1024, body) ||
        !catalog::parse_receipt(body, receipt, error) || receipt.id != id ||
        receipt.location != location || !folder_version(paths.target, id, installed) ||
        installed != receipt.content_version)
        return false;
    version = installed;
    return true;
}

std::string timestamp(const Environment &environment)
{
    if (environment.now)
        return environment.now();
    const std::time_t seconds = std::time(nullptr);
    std::tm parts{};
    gmtime_r(&seconds, &parts);
    char text[40]{};
    std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02dZ", parts.tm_year + 1900,
                  parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec);
    return text;
}

bool write_receipt(const Environment &environment, const Paths &paths, const std::string &id,
                   const std::string &location, const std::string &version, const std::string &tag,
                   const std::string &digest)
{
    if (!make_directory(paths.receipts))
        return false;
    catalog::Receipt receipt;
    receipt.id = id;
    receipt.location = location;
    receipt.content_version = version;
    receipt.release_tag = tag.empty() ? "unknown" : tag.substr(0, 128);
    receipt.digest = digest;
    receipt.installed_at = timestamp(environment);
    return write_durable(paths.receipts, paths.receipt, catalog::format_receipt(receipt));
}

bool free_space(const Environment &environment, const std::string &directory, std::uint64_t &bytes)
{
    return environment.space ? environment.space(directory, bytes)
                             : system::available_space(directory, bytes);
}

std::string megabytes(std::uint64_t bytes)
{
    return std::to_string((bytes + (1u << 20) - 1) >> 20) + " MB";
}

bool same_filesystem(const std::string &first, const std::string &second)
{
    struct stat a
    {
    }, b{};
    return stat(first.c_str(), &a) == 0 && stat(second.c_str(), &b) == 0 && a.st_dev == b.st_dev;
}

void pause(const Environment &environment, unsigned seconds, const std::atomic<bool> &cancelled)
{
    if (environment.wait)
        return environment.wait(seconds, cancelled);
    for (unsigned tick = 0; tick < seconds * 10 && !cancelled.load(); ++tick)
        usleep(100000);
}

// Removes a folder through the file worker when there is one, and by itself
// otherwise or when the worker left something behind.
bool discard(const Environment &environment, const std::string &path)
{
    if (environment.remove && kind(path) == Kind::directory && environment.remove(path) == 1)
        return true;
    return remove_tree(path);
}

// An update never deletes what it replaces. People keep their own files inside an app's
// folder (games, firmware, settings of an older version), and the new version's folder
// has none of them. So the previous folder is moved, whole, to the store's "previous"
// folder on the same drive, where the next update of that app replaces it. Nothing
// there is scanned by ShadowMountPlus (resolve checks it like the other work folders).
bool keep_previous(const Environment &environment, const Paths &paths)
{
    if (kind(paths.backup) == Kind::absent)
        return true;
    if (!make_directory(paths.keeps) || !discard(environment, paths.keep) ||
        rename(paths.backup.c_str(), paths.keep.c_str()) != 0)
        return false;
    sync_directory(paths.keeps);
    sync_directory(paths.backups);
    return true;
}

// Downloads to path and accepts the file only when its size and SHA-256 are
// the ones the signed catalog lists. Nothing else ever reads a partial file.
bool download(const Environment &environment, const catalog::Entry &entry, const std::string &path,
              net::Control &control, Progress &progress, std::string &error)
{
    const std::uint64_t limit = entry.size ? entry.size : catalog::kArtifactLimit;
    for (unsigned attempt = 0; attempt < 3; ++attempt)
    {
        if (!remove_tree(path))
        {
            error = "The download could not be saved";
            return false;
        }
        Writer writer;
        if (!environment.save || !environment.save(path, writer))
        {
            const int descriptor =
                open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
            if (descriptor < 0)
            {
                error = "The download could not be saved";
                return false;
            }
            writer.write = [descriptor](std::string_view chunk)
            {
                std::size_t done = 0;
                while (done < chunk.size())
                {
                    const auto count = write(descriptor, chunk.data() + done, chunk.size() - done);
                    if (count < 0 && errno == EINTR)
                        continue;
                    if (count <= 0)
                        return false;
                    done += static_cast<std::size_t>(count);
                }
                return true;
            };
            writer.finish = [descriptor]
            {
                const bool synced = fsync(descriptor) == 0;
                return close(descriptor) == 0 && synced;
            };
        }
        picosha2::hash256_one_by_one hash;
        std::uint64_t received = 0;
        bool saved = true;
        progress.done = 0;
        const auto response = environment.fetch(
            entry.artifact, limit,
            [&](std::string_view chunk)
            {
                if (!writer.write(chunk))
                    return saved = false;
                hash.process(chunk.begin(), chunk.end());
                received += chunk.size();
                progress.done = received;
                return true;
            },
            control);
        saved = writer.finish() && saved;
        writer = {};
        if (response.ok() && response.status == 200 && saved)
        {
            std::array<std::uint8_t, 32> digest{}, expected{};
            hash.finish();
            hash.get_hash_bytes(digest.begin(), digest.end());
            if ((entry.size && received != entry.size) ||
                !catalog::hex_bytes(entry.digest, expected) || digest != expected)
            {
                remove_tree(path);
                error = "The file doesn't match the listing";
                return false;
            }
            return true;
        }
        remove_tree(path);
        if (control.cancelled.load())
            error = "Cancelled";
        else if (!saved)
            error = "The download could not be saved";
        else
            error = response.error.empty() ? "The download failed" : response.error;
        if (control.cancelled.load() || !saved || error == "The download address is not allowed")
            return false;
        if (attempt < 2)
            pause(environment, 2u << attempt, control.cancelled);
    }
    return false;
}

// ShadowMountPlus copies an app's sce_sys once, when it first registers the
// title. After an update the store brings those copies up to date itself.
// Best effort: the app is installed whether or not this works.
void refresh_registered(const Environment &environment, const std::string &id,
                        const std::string &target)
{
    if (environment.registered.empty())
        return;
    const std::string source = target + "/sce_sys";
    std::vector<std::string> names;
    if (!list_files(source, 64, names))
        return;
    const std::string app = environment.registered + "/app/" + id;
    for (const auto &folder : {environment.registered + "/appmeta/" + id, app + "/sce_sys"})
        if (kind(folder) == Kind::directory)
            for (const auto &name : names)
                copy_file(source + "/" + name, folder + "/" + name);
    if (kind(app + "/icon0.png") == Kind::file)
        copy_file(source + "/icon0.png", app + "/icon0.png");
}

int running(const Environment &environment, const std::string &id)
{
    return environment.running ? environment.running(id) : -1;
}
const char *running_refusal(int state)
{
    return state == 1 ? "Close the app first"
                      : "The app's running state couldn't be checked, so it wasn't changed";
}
} // namespace

#define STORE_STEP(name)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (environment.interrupt && environment.interrupt(name))                                  \
        {                                                                                          \
            result.interrupted = true;                                                             \
            result.error = "Interrupted";                                                          \
            return result;                                                                         \
        }                                                                                          \
    } while (false)

Result apply(const Environment &environment, const Request &request, net::Control &control,
             Progress &progress)
{
    Result result;
    const auto &entry = request.entry;
    const auto &id = entry.id;
    const auto fail = [&](std::string message)
    {
#ifdef STORE_DEBUG_TRACE
        const int saved = errno;
        diag::trace("install %s refused: %s (errno %d %s; location %s, store folder %s)",
                    id.c_str(), message.c_str(), saved, std::strerror(saved),
                    request.location.c_str(),
                    environment.root.empty() ? "none" : environment.root.c_str());
#endif
        progress.phase = static_cast<int>(Phase::idle);
        result.error = std::move(message);
        return result;
    };
    std::array<std::uint8_t, 32> digest{};
    if (!environment.fetch || environment.root.empty())
        return fail("Installing isn't available: no permission to write");
    if (!catalog::title_id(id) || entry.status != "available")
        return fail("This app can't be installed yet");
    if (entry.format != "zip")
        return fail("Can't be installed by this version of ProsperoStore");
    if (!catalog::artifact_url(entry.artifact))
        return fail("The download address isn't allowed");
    if (!catalog::hex_bytes(entry.digest, digest) || entry.size > catalog::kArtifactLimit)
        return fail("The listing can't be installed safely");
    if (!request.minimum_version.empty() && catalog::version(entry.content_version) &&
        entry.content_version < request.minimum_version)
        return fail("An older version than the catalog's current one was refused");
    const bool self = id == environment.self;
    if (self != request.self_update)
        return fail("ProsperoStore is updated separately");

    std::string error, previous;
    Paths paths;
    const auto existing = kind(request.location + "/" + id);
    // An update stays where the app is; the location was validated when it was installed.
    if (!resolve(environment, id, request.location, existing != Kind::directory, paths, error))
        return fail(error);
    if (kind(paths.journal) != Kind::absent)
        return fail("An interrupted operation must be recovered first");
    const bool update = existing == Kind::directory;
    if (update)
    {
        // The store itself has no receipt (it was put there by hand or by an
        // earlier version) and is, of course, running.
        if (self ? !folder_version(paths.target, id, previous)
                 : !managed(paths, id, request.location, previous))
            return fail("Installed outside ProsperoStore. Not managed by this app.");
        if (!catalog::update_available(previous, entry.content_version))
            return fail("The catalog doesn't list a newer version");
        if (const int state = self ? 0 : running(environment, id); state != 0)
            return fail(running_refusal(state));
    }
    else if (existing != Kind::absent)
        return fail("The install location holds something unexpected");
    else if (self)
        return fail("ProsperoStore isn't installed in this location");
    result.operation = update ? "update" : "install";
    if (kind(request.location) != Kind::directory)
        return fail("The location isn't available");
#ifdef STORE_DEBUG_TRACE
    for (const std::string *folder :
         {&request.location, static_cast<const std::string *>(&paths.work),
          static_cast<const std::string *>(&paths.staging),
          static_cast<const std::string *>(&paths.backups)})
    {
        struct stat info
        {
        };
        const int found = stat(folder->c_str(), &info);
        const int stat_errno = found == 0 ? 0 : errno;
        const bool made = make_directory(*folder);
        const int make_errno = made ? 0 : errno;
        diag::trace("install %s: %s stat %d mode %o uid %u; make folder %s (errno %d %s)",
                    id.c_str(), folder->c_str(), stat_errno,
                    found == 0 ? static_cast<unsigned>(info.st_mode) : 0U,
                    found == 0 ? static_cast<unsigned>(info.st_uid) : 0U, made ? "ok" : "failed",
                    make_errno, std::strerror(make_errno));
    }
#endif
    if (!make_directory(paths.work) || !make_directory(paths.staging) ||
        !make_directory(paths.backups))
        return fail("No permission to write to the location's drive");
    if (!same_filesystem(request.location, paths.staging))
        return fail("The location can't be changed in a single step on this drive");
    if (kind(paths.backup) != Kind::absent)
        return fail("An earlier update left a backup that needs attention");
    if (!discard(environment, paths.archive) || !discard(environment, paths.staged))
        return fail("The staging folder could not be cleaned");

    catalog::Journal journal;
    journal.operation = result.operation;
    journal.state = "staging";
    journal.id = id;
    journal.location = request.location;
    if (!write_journal(environment, paths, journal))
        return fail("The transaction could not be recorded");
    // Until the journal says otherwise, nothing outside staging has changed.
    const auto abandon = [&](std::string message)
    {
        const bool clean =
            discard(environment, paths.archive) && discard(environment, paths.staged);
        if (clean)
            clear_journal(environment, paths);
        return fail(std::move(message));
    };
    STORE_STEP("journal");

    std::uint64_t available = 0;
    if (!free_space(environment, paths.staging, available))
        return abandon("The location isn't available");
    if (entry.size > available || available - entry.size < kDownloadMargin)
        return abandon("Not enough space: " + megabytes(entry.size + kDownloadMargin) +
                       " needed, " + megabytes(available) + " free");
    progress.total = entry.size;
    progress.done = 0;
    progress.phase = static_cast<int>(Phase::downloading);
    if (!download(environment, entry, paths.archive, control, progress, error))
        return abandon(error);
    STORE_STEP("downloaded");

    progress.phase = static_cast<int>(Phase::verifying);
    ArchiveInfo info;
    if (!inspect_archive(paths.archive, id, info, error))
        return abandon(error);
    if (!free_space(environment, paths.staging, available))
        return abandon("The location isn't available");
    if (info.unpacked > available || available - info.unpacked < kUnpackMargin)
        return abandon("Not enough space: " + megabytes(info.unpacked + kUnpackMargin) +
                       " needed, " + megabytes(available) + " free");
    progress.total = info.unpacked;
    progress.done = 0;
    progress.phase = static_cast<int>(Phase::unpacking);
    ExtractTimes times;
    int unpacked = -1;
    if (environment.unpack)
        unpacked = environment.unpack(paths.archive, id, paths.staged, control.cancelled,
                                      progress.done, error, times);
    const bool helped = unpacked >= 0;
    if (unpacked < 0)
        unpacked = extract_archive(paths.archive, id, paths.staged, control.cancelled,
                                   progress.done, error, &times)
                       ? 1
                       : 0;
    if (unpacked != 1)
        return abandon(error);
    result.note =
        "files=" + std::to_string(times.files) + " unpack_ms=" + std::to_string(times.total_ms) +
        " write_ms=" + std::to_string(times.write_ms) +
        " sync_ms=" + std::to_string(times.sync_ms) + " worker=" + std::to_string(helped ? 1 : 0);
    STORE_STEP("unpacked");
    if (!discard(environment, paths.archive))
        return abandon("The staging folder could not be cleaned");
    if (!folder_version(paths.staged, id, result.version))
        return abandon("The archive isn't a valid app");
    if (update && !catalog::update_available(previous, result.version))
        return abandon("The download isn't newer than the installed version");
    if (control.cancelled.load())
        return abandon("Cancelled");

    // Everything is staged. Look again at what is about to be replaced.
    progress.phase = static_cast<int>(Phase::activating);
    if (update)
    {
        std::string current;
        if ((self ? !folder_version(paths.target, id, current)
                  : !managed(paths, id, request.location, current)) ||
            current != previous)
            return abandon("The installed app changed while the update was prepared");
        if (const int state = self ? 0 : running(environment, id); state != 0)
            return abandon(running_refusal(state));
    }
    else if (kind(paths.target) != Kind::absent)
        return abandon("The app was installed by something else meanwhile");
    journal.state = update ? "swap" : "activate";
    journal.content_version = result.version;
    journal.release_tag = entry.version.substr(0, 128);
    journal.digest = entry.digest;
    if (!write_journal(environment, paths, journal))
    {
        journal.state = "staging"; // The earlier record still stands: clean up under it.
        return abandon("The transaction could not be recorded");
    }
    STORE_STEP("journaled");
    if (update)
    {
        if (rename(paths.target.c_str(), paths.backup.c_str()) != 0)
            return abandon("The installed app could not be moved aside");
        sync_directory(request.location);
        sync_directory(paths.backups);
        STORE_STEP("moved-old");
        if (rename(paths.staged.c_str(), paths.target.c_str()) != 0)
        {
            if (rename(paths.backup.c_str(), paths.target.c_str()) != 0)
                return fail("The update failed. Restart ProsperoStore to restore the app.");
            sync_directory(request.location);
            return abandon("The new version could not be put in place");
        }
    }
    else if (rename(paths.staged.c_str(), paths.target.c_str()) != 0)
        return abandon("The app could not be put in place");
    sync_directory(request.location);
    sync_directory(paths.staging);
    STORE_STEP("placed");
    if (self)
    {
        // The running store still uses the files of the folder that was moved
        // aside. It stays until the next start, when recovery writes the
        // receipt, removes it and closes the journal.
        // The console's staged copy of sce_sys is what the next start reads
        // its version from, so it is brought up to date now.
        refresh_registered(environment, id, paths.target);
        progress.phase = static_cast<int>(Phase::idle);
        result.ok = true;
        result.restart = true;
        return result;
    }
    // From here the journal is kept on failure, so the next start finishes the job.
    if (!write_receipt(environment, paths, id, request.location, result.version, entry.version,
                       entry.digest))
        return fail("Installed, but not recorded yet. Restart ProsperoStore to finish.");
    STORE_STEP("recorded");
    // The journal stays on failure, so the next start sets the previous version aside.
    if (update && !keep_previous(environment, paths))
        return fail("Updated, but the previous version is not set aside yet. Restart "
                    "ProsperoStore to finish.");
    if (update)
        result.kept_at = paths.keep;
    if (update)
        refresh_registered(environment, id, paths.target);
    STORE_STEP("cleaned");
    if (!clear_journal(environment, paths))
        return fail("Installed, but the transaction could not be closed.");
    progress.phase = static_cast<int>(Phase::idle);
    result.ok = true;
    return result;
}

Result uninstall(const Environment &environment, const std::string &id, const std::string &location,
                 Progress &progress)
{
    Result result;
    result.operation = "uninstall";
    const auto fail = [&](std::string message)
    {
        progress.phase = static_cast<int>(Phase::idle);
        result.error = std::move(message);
        return result;
    };
    if (id == environment.self)
        return fail("ProsperoStore can't uninstall itself");
    std::string error;
    Paths paths;
    if (!resolve(environment, id, location, false, paths, error))
        return fail(error);
    if (kind(paths.journal) != Kind::absent)
        return fail("An interrupted operation must be recovered first");
    if (!managed(paths, id, location, result.version))
        return fail("Installed outside ProsperoStore. Not managed by this app.");
    if (const int state = running(environment, id); state != 0)
        return fail(running_refusal(state));
    if (!make_directory(paths.work) || !make_directory(paths.trashes) ||
        !discard(environment, paths.trash))
        return fail("No permission to write to the location's drive");
    catalog::Journal journal;
    journal.operation = "uninstall";
    journal.state = "remove";
    journal.id = id;
    journal.location = location;
    if (!write_journal(environment, paths, journal))
        return fail("The transaction could not be recorded");
    STORE_STEP("journal");
    progress.phase = static_cast<int>(Phase::removing);
    // One rename takes the whole app out of the scanned location.
    if (rename(paths.target.c_str(), paths.trash.c_str()) != 0)
    {
        clear_journal(environment, paths);
        return fail("The app could not be removed");
    }
    sync_directory(location);
    sync_directory(paths.trashes);
    STORE_STEP("moved");
    const std::time_t removing = std::time(nullptr);
    if (!discard(environment, paths.trash))
        return fail("Uninstalled, but its files are still being removed.");
    result.note =
        "remove_s=" + std::to_string(static_cast<long long>(std::time(nullptr) - removing));
    STORE_STEP("deleted");
    if (unlink(paths.receipt.c_str()) != 0 && errno != ENOENT)
        return fail("Uninstalled, but the receipt could not be removed.");
    sync_directory(paths.receipts);
    STORE_STEP("unrecorded");
    if (!clear_journal(environment, paths))
        return fail("Uninstalled, but the transaction could not be closed.");
    progress.phase = static_cast<int>(Phase::idle);
    result.ok = true;
    return result;
}

Result adopt(const Environment &environment, const std::string &id, const std::string &location)
{
    Result result;
    result.operation = "adopt";
    std::string error;
    Paths paths;
    if (id == environment.self)
        result.error = "ProsperoStore manages itself";
    else if (!resolve(environment, id, location, true, paths, error))
        result.error = error;
    else if (kind(paths.journal) != Kind::absent)
        result.error = "An interrupted operation must be recovered first";
    else if (!folder_version(paths.target, id, result.version))
        result.error = "The folder doesn't hold this app";
    else if (!write_receipt(environment, paths, id, location, result.version, "adopted",
                            std::string(64, '0')))
        result.error = "The receipt could not be written";
    else
        result.ok = true;
    return result;
}

Result recover(const Environment &environment)
{
    Result result;
    const std::string path = environment.root + "/journal.json";
    const auto fail = [&](std::string message)
    {
        result.error = std::move(message);
        return result;
    };
    const auto found = kind(path);
    // A journal that was being written when the power went is the previous one, or none.
    unlink((path + ".tmp").c_str());
    if (found == Kind::absent)
    {
        result.ok = true;
        return result;
    }
    std::string body, error;
    catalog::Journal journal;
    Paths paths;
    const char *step = nullptr;
    if (found != Kind::file)
        step = "not a regular file";
    else if (!read_small(path, 16 * 1024, body))
        step = "can't be read";
    else if (!catalog::parse_journal(body, journal, error))
        step = "can't be parsed";
    else if (!resolve(environment, journal.id, journal.location, false, paths, error))
        step = "names a place that can't be used";
    if (step)
    {
#ifdef STORE_DEBUG_TRACE
        const int saved = errno;
        struct stat info
        {
        };
        const int lstat_errno = lstat(path.c_str(), &info) == 0 ? 0 : errno;
        diag::trace("install journal %s: kind %d, lstat errno %d, errno %d, %s; body: %.200s", step,
                    static_cast<int>(found), lstat_errno, saved, error.c_str(), body.c_str());
#endif
        return fail("The record of an interrupted operation can't be read. Installing is off.");
    }
    result.operation = journal.operation;
    const auto &id = journal.id;
    const auto placed = kind(paths.target), staged = kind(paths.staged);
    const auto close = [&]
    {
        if (!discard(environment, paths.archive) || !discard(environment, paths.staged) ||
            !clear_journal(environment, paths))
            return fail("An interrupted operation could not be cleaned up");
        result.ok = true;
        return result;
    };
    const auto record = [&]
    {
        return write_receipt(environment, paths, id, journal.location, journal.content_version,
                             journal.release_tag, journal.digest);
    };
    std::string version;
    if (journal.state == "staging")
        return close();
    if (id == environment.self && journal.state == "swap" && !environment.self_version.empty() &&
        environment.self_version != journal.content_version)
    {
        // Still the old store, started from the folder that was moved aside:
        // everything waits for the new one.
        result.ok = true;
        result.restart = true;
        return result;
    }
    if (journal.state == "activate")
    {
        // The staged folder is gone exactly when the rename put it in place.
        if (placed == Kind::directory && staged == Kind::absent &&
            folder_version(paths.target, id, version) && version == journal.content_version)
        {
            if (!record())
                return fail("An interrupted install could not be recorded");
            result.version = version;
        }
        return close();
    }
    if (journal.state == "swap")
    {
        const auto backup = kind(paths.backup);
        if (placed == Kind::absent && backup == Kind::directory)
        {
            // Stopped between the two renames: the previous version goes back.
            if (rename(paths.backup.c_str(), paths.target.c_str()) != 0)
                return fail("The previous version could not be restored");
            sync_directory(journal.location);
            return close();
        }
        if (placed == Kind::directory && folder_version(paths.target, id, version) &&
            version == journal.content_version)
        {
            // The store's own previous folder holds nothing of the user's.
            if (!record() || !(id == environment.self ? discard(environment, paths.backup)
                                                      : keep_previous(environment, paths)))
                return fail("An interrupted update could not be finished");
            if (id != environment.self)
                result.kept_at = paths.keep;
            refresh_registered(environment, id, paths.target);
            result.version = version;
            return close();
        }
        if (backup != Kind::absent)
            return fail("An interrupted update left a backup that needs attention");
        return close(); // The swap never started: the installed version is untouched.
    }
    // Uninstall. The trash folder exists exactly when the app left its location.
    const auto trash = kind(paths.trash);
    if (trash == Kind::absent && placed != Kind::absent)
        return close(); // Never started: the app and its receipt stay.
    if (!discard(environment, paths.trash))
        return fail("An interrupted uninstall could not be finished");
    if (unlink(paths.receipt.c_str()) != 0 && errno != ENOENT)
        return fail("An interrupted uninstall could not be finished");
    sync_directory(paths.receipts);
    return close();
}
} // namespace store::install
