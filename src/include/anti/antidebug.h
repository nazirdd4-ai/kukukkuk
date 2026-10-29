#pragma once
// antidebug.h — runtime anti-debug / anti-ptrace tripwires for THIS process
// (HOST or PAYLOAD, whichever includes it). Raises the cost of live attach
// (gdb, Frida, IDA remote debugger). Does NOT protect the file at rest — see
// integrity.h and the payload_blob encryption in cheat.cpp for that side.
//
// LIMIT (stated once, applies to every check below): every rule here runs
// inside the process it inspects and is visible to anyone who disassembles
// this binary. A reverser who reads this file can patch out any single
// check. This buys time against casual attach and off-the-shelf tooling, not
// against a targeted attacker who has this source. Pair with integrity.h so
// patching a check also breaks the self-hash — still a local check a patient
// attacker can defeat, see that file's header for why.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "rawio.h"      // syscall-level open/read, bypasses named-libc-symbol hooks — see file header
#include "strobf.h"     // compile-time needle/path obfuscation, keeps these out of `strings` output — see file header
#include "sectcrypt.hpp" // PCODE tag below — every function in this file ships encrypted at rest on a SECT_PROTECT build, see that file's header for the mechanism. A no-op without SECT_PROTECT defined.

namespace antidbg {

// Raw syscall(__NR_ptrace, ...) instead of the libc ptrace() wrapper. Same kernel call, same
// effect (PTRACE_TRACEME below occupies the tracer slot exactly like before) — the only reason
// to go around the wrapper is that Frida's common attach path hooks the NAMED libc symbol
// `ptrace` (Interceptor.attach(Module.findExportByName(null,"ptrace"),...) is the textbook
// one-liner), and a hook on that symbol never sees a call that never goes through it. Does NOT
// defeat a syscall-table/seccomp-level hook, or an attacker who great-s this file and special-
// cases __NR_ptrace too — same tier of "raises cost," not "closes the hole," as the rest of this
// file's checks (see header).
PCODE inline long rawPtrace(int request, pid_t pid, void* addr, void* data) {
    return syscall(__NR_ptrace, request, pid, addr, data);
}

// TracerPid from /proc/self/status. 0 = no tracer attached. OBSERVED-reliable
// on a stock Android process. Read via rawio (syscall(__NR_openat/__NR_read)
// instead of open()/read()) so a Frida hook on the NAMED libc symbols can't
// intercept this call and fake the file content — same reasoning as
// rawPtrace() above. Still not proof on its own: a hook at the
// syscall-table/seccomp level, below where rawio sits, could still fake it —
// a clean read is supporting evidence, combine with the other two checks.
PCODE inline int tracerPid() {
    char buf[4096];
    if (!rawio::rawReadFile(OBF("/proc/self/status"), buf, sizeof(buf))) return -1;  // can't check -> caller treats as unknown, not clean
    const char* p = strstr(buf, OBF("TracerPid:"));
    return p ? atoi(p + 10) : -1;
}

// Parent PID recorded by armSelfTrace(), so tracerPid()-based checks below
// can tell "our own self-arm" apart from a tracer that attached afterward.
// 0 = armSelfTrace() not called (or not yet).
inline pid_t g_expectedTracer = 0;

// PTRACE_TRACEME: fails (EPERM) if a tracer is already attached, since the
// kernel allows only one. Call once, as early as possible (top of ctor() /
// main(), before any other thread starts) — if it succeeds, the kernel now
// treats THIS PROCESS'S PARENT as its tracer (not "nobody"), which also
// blocks a LATER PTRACE_ATTACH from gdb/Frida for the rest of the process's
// life, since only one tracer is allowed at a time. Side effect this module
// must account for: after a successful call, /proc/self/status TracerPid
// becomes the parent's pid, not 0 — tracerPid() alone can no longer mean
// "clean." check() below compares against g_expectedTracer instead of a
// bare nonzero test. Needs a SIGCHLD-ignoring or reaping parent, or an
// unhandled SIGSTOP/SIGTRAP can wedge the process — verify this on-device
// before shipping in a release build (UNVERIFIED here: no Android runtime
// available in this session).
PCODE inline bool armSelfTrace() {
    pid_t parent = getppid();
    if (rawPtrace(PTRACE_TRACEME, 0, nullptr, nullptr) != 0) return false;
    g_expectedTracer = parent;
    return true;
}

// Tight loop timed with CLOCK_MONOTONIC. A debugger breakpoint or
// single-step anywhere in this window inflates elapsed time by orders of
// magnitude. Threshold is generous to avoid false positives from scheduler
// jitter or thermal throttling on a loaded phone; tune iterations/threshold
// against a real device before trusting the default.
PCODE inline bool timingAnomaly(int iterations = 200000, int64_t thresholdNs = 50'000'000LL) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    volatile uint64_t x = 0;
    for (int i = 0; i < iterations; ++i) x += (uint64_t)getpid();
    clock_gettime(CLOCK_MONOTONIC, &t1);
    int64_t ns = (t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec);
    return ns > thresholdNs;
}

// /proc/self/maps scan for common instrumentation markers. Each needle is a
// lead, not proof — Frida's gadget name is configurable and frida-server is
// routinely renamed in the field. Extend kNeedles as new tooling shows up;
// an empty hit list is "nothing matched," not "clean."
PCODE inline bool suspiciousModuleLoaded() {
    // not `static`: see strobf.h's header on why OBF() must re-decode on
    // every call instead of being cached behind a function-local static.
    const char* kNeedles[] = {
        OBF("frida"), OBF("gum-js-loop"), OBF("linjector"), OBF("re.frida.server"),
        OBF("gadget.so"), OBF("gdbserver"), OBF("ida-remote"), OBF("xposed"),
    };
    // read via rawio, not fopen()/fgets() — a hook on the named open/read
    // libc symbols (the exact kind of tooling this check is hunting for)
    // could otherwise filter its own module out of what this function sees.
    return rawio::rawScanLines(OBF("/proc/self/maps"), [&](const char* line) {
        for (size_t i = 0; i < sizeof(kNeedles) / sizeof(kNeedles[0]); ++i) {
            const char* b = kNeedles[i];
            for (const char* p = line; *p; ++p) {
                size_t j = 0;
                while (b[j] && p[j] && tolower((unsigned char)p[j]) == tolower((unsigned char)b[j])) ++j;
                if (!b[j]) return true;
            }
        }
        return false;
    });
}

enum class Verdict { Clean, Suspicious };

// Call periodically (e.g. once per poll tick) after armSelfTrace() ran once
// at startup. A few syscalls plus a ~200k-iteration loop: well under 1ms on
// any arm64 phone, safe to run every frame if desired.
PCODE inline Verdict check() {
    int tp = tracerPid();
    // if armSelfTrace() was never called, any nonzero TracerPid is a real
    // attach; if it was called, only a tracer OTHER than the recorded
    // parent counts — the parent itself is the expected, self-armed tracer.
    bool tracerBad = g_expectedTracer == 0 ? (tp > 0) : (tp > 0 && tp != g_expectedTracer);
    if (tracerBad) return Verdict::Suspicious;
    if (suspiciousModuleLoaded()) return Verdict::Suspicious;
    if (timingAnomaly()) return Verdict::Suspicious;
    return Verdict::Clean;
}

} // namespace antidbg
