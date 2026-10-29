#pragma once
// rawio.h — syscall(2)-level file I/O, bypassing the named libc wrappers
// (open/openat/read/close/access/faccessat/fopen). Same rationale as
// antidebug.h's rawPtrace(): Frida's textbook attach hooks a NAMED exported
// symbol — Interceptor.attach(Module.findExportByName(null,"open"), ...) is
// the one-liner every tutorial ships — so a check that reads
// /proc/self/status, /proc/self/maps, or a build-prop/cpuinfo file through
// fopen()/open()/access() can have its view of the filesystem quietly
// rewritten by a hook that fakes a clean answer. A hook on the named symbol
// never sees a call that never goes through it.
//
// LIMIT (same tier as every other file in this directory, stated once):
// this raises the cost of the common attach path — it does NOT survive a
// syscall-table/seccomp-bpf-level hook, or an attacker who reads this file
// and special-cases these syscall numbers too. See antidebug.h's header for
// the general stance on what "raise cost" buys and doesn't.

#include <cstddef>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "sectcrypt.hpp" // PCODE tag below — every function in this file ships encrypted at rest on a SECT_PROTECT build, see that file's header for the mechanism. A no-op without SECT_PROTECT defined.

namespace rawio {

PCODE inline int rawOpenRO(const char* path) {
    return (int)syscall(__NR_openat, AT_FDCWD, path, O_RDONLY | O_CLOEXEC);
}

PCODE inline ssize_t rawRead(int fd, void* buf, size_t n) {
    return syscall(__NR_read, fd, buf, n);
}

PCODE inline void rawClose(int fd) {
    syscall(__NR_close, fd);
}

// F_OK existence check via faccessat — the direct-syscall equivalent of
// access(path, F_OK).
PCODE inline bool rawExists(const char* path) {
    return syscall(__NR_faccessat, AT_FDCWD, path, F_OK, 0) == 0;
}

// Reads up to bufSize-1 bytes of `path` into buf, NUL-terminated. False
// (buf left empty) on any open/read failure — "can't check" is not proof of
// anything, same stance every caller in this directory takes.
PCODE inline bool rawReadFile(const char* path, char* buf, size_t bufSize) {
    buf[0] = 0;
    int fd = rawOpenRO(path);
    if (fd < 0) return false;
    ssize_t n = rawRead(fd, buf, bufSize - 1);
    rawClose(fd);
    if (n <= 0) return false;
    buf[n] = 0;
    return true;
}

// Streams `path` through a fixed-size buffer and calls onLine(const char*)
// once per '\n'-terminated line (NUL-terminated, newline stripped) plus any
// trailing partial line at EOF. Stops early the first time onLine returns
// true. Returns whether a line matched — false also covers "file couldn't
// be opened", same "can't check" stance as the rest of this header. A line
// longer than 1023 bytes is truncated, not split — fine for /proc/self/maps
// and /proc/cpuinfo lines, which never run that long.
template <typename Fn>
inline bool rawScanLines(const char* path, Fn&& onLine) {
    int fd = rawOpenRO(path);
    if (fd < 0) return false;
    char buf[4096];
    char line[1024];
    size_t lineLen = 0;
    bool found = false;
    ssize_t n;
    while (!found && (n = rawRead(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n && !found; ++i) {
            char c = buf[i];
            if (c == '\n') {
                line[lineLen] = 0;
                if (onLine(line)) found = true;
                lineLen = 0;
            } else if (lineLen < sizeof(line) - 1) {
                line[lineLen++] = c;
            }
        }
    }
    if (!found && lineLen > 0) { line[lineLen] = 0; if (onLine(line)) found = true; }
    rawClose(fd);
    return found;
}

} // namespace rawio
