// ProsperoStore - The debug build's trace: what the store found at each step, for reports.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diag/trace.hpp"
#include "platform/ps5/system.hpp"
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

extern "C"
{
    struct SwVersion
    {
        std::uint64_t size;
        char text[0x1c];
        std::uint32_t version;
    };
    int sceKernelGetSystemSwVersion(SwVersion *version);
    int sceNetSocket(const char *name, int domain, int type, int protocol);
    int sceNetConnect(int socket, const void *address, std::uint32_t length);
    int sceNetSetsockopt(int socket, int level, int option, const void *value, std::uint32_t size);
    int sceNetSocketClose(int socket);
}

namespace store::diag
{
namespace
{
std::mutex guard;
std::vector<std::string> lines;
std::string file;
bool writing = false;
constexpr std::size_t kLines = 600; // kept in memory; the file has them all

// The first place the trace can be written: /data once the store reaches it, else a
// USB drive (which ShadowMountPlus 1.7 mounts into the sandbox), else nowhere.
void open_file()
{
    const char *candidates[] = {
        "/data/prosperostore/debug-trace.txt", "/mnt/usb0/ProsperoStore-debug-trace.txt",
        "/mnt/usb1/ProsperoStore-debug-trace.txt", "/mnt/ext0/ProsperoStore-debug-trace.txt"};
    for (const char *candidate : candidates)
    {
        if (!file.empty() && file == candidate)
            return;
        if (std::strncmp(candidate, "/data/", 6) == 0)
            (void)mkdir("/data/prosperostore", 0777);
        const int descriptor = open(candidate, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (descriptor < 0)
            continue;
        // A better place than before: everything so far goes there too.
        for (const auto &line : lines)
        {
            (void)write(descriptor, line.data(), line.size());
            (void)write(descriptor, "\n", 1);
        }
        close(descriptor);
        file = candidate;
        return;
    }
}

void append(const std::string &line)
{
    if (file.empty())
        return;
    const int descriptor = open(file.c_str(), O_WRONLY | O_APPEND);
    if (descriptor < 0)
        return;
    (void)write(descriptor, line.data(), line.size());
    (void)write(descriptor, "\n", 1);
    close(descriptor);
}

std::string probe_write(const char *folder)
{
    const std::string path = std::string(folder) + "/.prosperostore-probe";
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (descriptor < 0)
        return std::string("no (") + std::strerror(errno) + ")";
    const bool wrote = write(descriptor, "probe", 5) == 5;
    close(descriptor);
    unlink(path.c_str());
    return wrote ? "yes" : "opened but not written";
}

std::string probe_loader()
{
    struct Address
    {
        std::uint8_t length, family;
        std::uint16_t port;
        std::uint32_t address;
        std::uint16_t virtual_port;
        std::uint8_t zero[6];
    };
    const int socket = sceNetSocket("trace_loader", 2, 1, 6);
    if (socket < 0)
        return "no socket";
    constexpr int connect_us = 2'000'000;
    (void)sceNetSetsockopt(socket, 0xffff, 0x1109, &connect_us, sizeof(connect_us));
    constexpr std::uint16_t port = 9021;
    const Address address{sizeof(Address), 2, static_cast<std::uint16_t>((port << 8) | (port >> 8)),
                          0x0100007f,      0, {0}};
    const int result = sceNetConnect(socket, &address, sizeof(address));
    (void)sceNetSocketClose(socket);
    char text[48];
    std::snprintf(text, sizeof(text), result < 0 ? "no answer (0x%08x)" : "answers",
                  static_cast<unsigned>(result));
    return text;
}
} // namespace

void trace(const char *format, ...)
{
    char text[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    std::lock_guard lock(guard);
    if (lines.size() >= kLines)
        lines.erase(lines.begin() + 40, lines.begin() + 140); // keep the start-up lines
    lines.emplace_back(text);
    if (!writing)
        return;
    hui::sys::log("[STORE] trace %s", text);
    // Until the file is in /data, look for a better place each time (a few lines a start).
    if (file.rfind("/data/", 0) != 0)
        open_file();
    else
        append(lines.back());
}

void set_enabled(bool on)
{
    std::lock_guard lock(guard);
    if (on == writing)
        return;
    writing = on;
    if (!on)
        return;
    // What was kept while it was off goes out now: the kernel log, then the file.
    for (const auto &line : lines)
        hui::sys::log("[STORE] trace %s", line.c_str());
    file.clear();
    open_file();
}

bool enabled()
{
    std::lock_guard lock(guard);
    return writing;
}

std::vector<std::string> trace_lines()
{
    std::lock_guard lock(guard);
    return lines;
}

std::string trace_file()
{
    std::lock_guard lock(guard);
    return file;
}

void trace_firmware()
{
    SwVersion version{sizeof(SwVersion), {}, 0};
    const int read = sceKernelGetSystemSwVersion(&version);
    trace("firmware: %s (0x%08x)", read == 0 ? version.text : "unknown",
          static_cast<unsigned>(version.version));
    const std::time_t now = std::time(nullptr);
    char clock[32];
    std::strftime(clock, sizeof(clock), "%Y-%m-%d %H:%M UTC", std::gmtime(&now));
    trace("console clock: %s", clock);
}

void trace_console(const char *when)
{
    if (std::strcmp(when, "after elevation") != 0)
        trace("payload loader on port 9021: %s", probe_loader().c_str());
    struct stat info
    {
    };
    trace("[%s] /data visible: %s, writable: %s", when, stat("/data", &info) == 0 ? "yes" : "no",
          probe_write("/data").c_str());
    trace("[%s] /system_ex/app/PPSA99000: %s", when,
          stat("/system_ex/app/PPSA99000/eboot.bin", &info) == 0 ? "found" : "not found");
    trace("[%s] /app0: %s", when, stat("/app0/eboot.bin", &info) == 0 ? "found" : "not found");
    const char *journal = "/data/prosperostore/journal.json";
    if (lstat(journal, &info) == 0)
        trace("[%s] journal.json: present, mode %o, %lld bytes, uid %u", when,
              static_cast<unsigned>(info.st_mode), static_cast<long long>(info.st_size),
              static_cast<unsigned>(info.st_uid));
    else
        trace("[%s] journal.json: lstat errno %d (%s)", when, errno, std::strerror(errno));
    trace("[%s] process uid %u gid %u", when, static_cast<unsigned>(getuid()),
          static_cast<unsigned>(getgid()));
    for (const char *folder : {"/data", "/data/prosperostore", "/data/homebrew"})
    {
        if (stat(folder, &info) == 0)
            trace("[%s] %s: mode %o, uid %u, gid %u", when, folder,
                  static_cast<unsigned>(info.st_mode), static_cast<unsigned>(info.st_uid),
                  static_cast<unsigned>(info.st_gid));
        else
            trace("[%s] %s: stat errno %d (%s)", when, folder, errno, std::strerror(errno));
        if (DIR *listed = opendir(folder))
        {
            int count = 0;
            while (readdir(listed))
                ++count;
            closedir(listed);
            trace("[%s] %s: listed, %d entries", when, folder, count);
        }
        else
            trace("[%s] %s: opendir errno %d (%s)", when, folder, errno, std::strerror(errno));
    }
    // A fresh folder: can the store use what it just made?
    {
        const char *fresh = "/data/.prosperostore-probe-dir";
        const char *inner = "/data/.prosperostore-probe-dir/file";
        const int made = mkdir(fresh, 0777);
        const int made_errno = made == 0 ? 0 : errno;
        const int fd = open(inner, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        const int open_errno = fd >= 0 ? 0 : errno;
        if (fd >= 0)
            close(fd);
        const int looked = lstat(inner, &info);
        const int look_errno = looked == 0 ? 0 : errno;
        DIR *listed = opendir(fresh);
        const int list_errno = listed ? 0 : errno;
        if (listed)
            closedir(listed);
        trace("[%s] fresh folder: mkdir %d/%d, create file %d, lstat file %d, opendir %d", when,
              made, made_errno, open_errno, look_errno, list_errno);
        unlink(inner);
        rmdir(fresh);
    }
    if (DIR *folder = opendir("/data/prosperostore"))
    {
        std::string names;
        while (const dirent *entry = readdir(folder))
            if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0 &&
                names.size() < 300)
                names += std::string(names.empty() ? "" : ", ") + entry->d_name;
        closedir(folder);
        trace("[%s] /data/prosperostore holds: %s", when,
              names.empty() ? "nothing" : names.c_str());
    }
    else
        trace("[%s] /data/prosperostore can't be listed (%s)", when, std::strerror(errno));
}
} // namespace store::diag
