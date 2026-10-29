#pragma once
// antivm.h — Android emulator / virtualized-device tripwires (goldfish/ranchu
// AOSP emulator, Genymotion, Nox/MEmu/LDPlayer/BlueStacks x86 farms, generic
// QEMU). This is the Android analogue of a desktop anti-VM check: these
// emulators are how click-farm / macro / multi-instance botting and a chunk
// of automated reversing actually run at scale, and they're cheap to flag
// because the images rarely bother hiding their own fingerprints.
//
// HONEST LIMIT (stated once, applies to every check below): every signal
// here is a build-prop string, a device file, or a sensor/hardware list —
// all of it userspace-visible and all of it spoofable by a maintained
// Magisk/Xposed hider ("fake build prop", "Magisk Hide", libhoudini pass-
// through profiles that rewrite ro.product.* to a real device's values). A
// patient operator running a hidden emulator defeats this file. What it
// catches is the common case: stock Genymotion/Nox/LDPlayer/the AOSP
// emulator running with factory settings, which is most of them. Combine
// with antidebug.h's checks (a farm operator attaching Frida to automate
// input is a debugger-shaped problem, not a VM-shaped one) and the license
// server for anything that actually has to hold against a targeted attacker.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/system_properties.h>
#include <unistd.h>

#include "rawio.h"      // syscall-level open/read/access, bypasses named-libc-symbol hooks — see file header
#include "strobf.h"     // compile-time needle/path obfuscation, keeps these out of `strings` output — see file header
#include "sectcrypt.hpp" // PCODE tag below — every function in this file ships encrypted at rest on a SECT_PROTECT build, see that file's header for the mechanism. A no-op without SECT_PROTECT defined.

namespace antivm {

// __system_property_get wrapper: reads a single Android build prop
// (ro.product.model, ro.kernel.qemu, ...) into out[PROP_VALUE_MAX]. Returns
// false (out left empty) if the prop is unset or the symbol errors out —
// treated the same as "no match" by every caller below, same stance as
// antidebug.h's "can't check -> don't block the feature" rule.
//
// UNLIKE the file reads below, this one is NOT routed through rawio — bionic
// property reads don't go through open()/read() at all (the property area is
// a shared-memory region, not a path), so there is no openat/read syscall to
// bypass a hook with. It's still a named, exported symbol a Frida hook can
// intercept (Interceptor.attach(Module.findExportByName(null,
// "__system_property_get"),...) is a one-liner same as any other named-
// symbol hook). Defeating that needs parsing /dev/__properties__ directly —
// out of scope here; suspiciousFiles()/suspiciousCpuinfo() below don't
// depend on this function, so they still hold if this one gets hooked.
PCODE inline bool getProp(const char* name, char out[PROP_VALUE_MAX]) {
    out[0] = 0;
    return __system_property_get(name, out) > 0;
}

PCODE inline bool iStrstr(const char* hay, const char* needle) {
    if (!hay || !needle || !*needle) return false;
    size_t nlen = strlen(needle);
    for (const char* p = hay; *p; ++p) {
        size_t j = 0;
        while (j < nlen && p[j] && tolower((unsigned char)p[j]) == tolower((unsigned char)needle[j])) ++j;
        if (j == nlen) return true;
    }
    return false;
}

// Build-prop fingerprints. ro.kernel.qemu / ro.boot.qemu are set to "1" by
// the stock AOSP emulator image itself (not spoofing, just how it boots) —
// single strongest signal here. The rest are substring hits against known
// emulator/vendor strings in fields every device exposes.
PCODE inline bool suspiciousBuildProps() {
    char v[PROP_VALUE_MAX];

    if (getProp(OBF("ro.kernel.qemu"), v) && v[0] == '1') return true;
    if (getProp(OBF("ro.boot.qemu"), v) && v[0] == '1') return true;
    if (getProp(OBF("ro.kernel.qemu.gles"), v) && v[0]) return true;

    // not `static`: see strobf.h's header on why OBF() must re-decode on
    // every call instead of being cached behind a function-local static.
    const char* kPropNames[] = {
        OBF("ro.product.model"), OBF("ro.product.manufacturer"), OBF("ro.product.brand"),
        OBF("ro.product.device"), OBF("ro.product.board"), OBF("ro.hardware"),
        OBF("ro.hardware.chipname"), OBF("ro.build.fingerprint"), OBF("ro.build.product"),
        OBF("ro.bootimage.build.fingerprint"),
    };
    const char* kNeedles[] = {
        OBF("goldfish"), OBF("ranchu"), OBF("generic_x86"), OBF("vbox"), OBF("virtualbox"),
        OBF("genymotion"), OBF("genymobile"), OBF("nox"), OBF("noxplayer"), OBF("bignox"),
        OBF("memu"), OBF("ldplayer"), OBF("bluestacks"), OBF("andy"), OBF("droid4x"), OBF("sdk_gphone"),
        OBF("sdk_google"), OBF("google_sdk"), OBF("emulator"), OBF("qemu"), OBF("ttvm"),
    };
    for (const char* pn : kPropNames) {
        if (!getProp(pn, v)) continue;
        for (const char* needle : kNeedles)
            if (iStrstr(v, needle)) return true;
    }
    return false;
}

// Device nodes / files present only on an emulator image, never a factory
// retail device. Existence check only, via rawio::rawExists (syscall-level
// faccessat, not the named access()/faccessat() libc symbols — same
// hook-dodging reasoning as the rest of this directory) — cheap and doesn't
// need any special permission.
PCODE inline bool suspiciousFiles() {
    // not `static`: see strobf.h's header on why OBF() must re-decode on
    // every call instead of being cached behind a function-local static.
    const char* kPaths[] = {
        OBF("/dev/qemu_pipe"),
        OBF("/dev/socket/qemud"),
        OBF("/dev/socket/genyd"),
        OBF("/dev/socket/baseband_genyd"),
        OBF("/system/lib/libc_malloc_debug_qemu.so"),
        OBF("/sys/qemu_trace"),
        OBF("/system/bin/qemu-props"),
        OBF("/system/bin/windows_bridge.sh"),   // Nox
        OBF("/mnt/windows/BstSharedFolder"),    // BlueStacks
        OBF("/system/bin/microvirt-prop"),      // MEmu
    };
    for (const char* p : kPaths)
        if (rawio::rawExists(p)) return true;
    return false;
}

// /proc/cpuinfo "Hardware:" line on real silicon names an actual SoC
// (qcom, exynos, mt6...); emulators routinely leave it literally naming the
// virtual platform, or blank/"Goldfish"/"Ranchu"/"VirtIO". A hit here is
// supporting evidence, same tier as antidebug.h's TracerPid note — combine
// with suspiciousBuildProps(), don't trust alone (a real device can omit
// the Hardware line too, and that's not itself suspicious).
PCODE inline bool suspiciousCpuinfo() {
    // rawio, not fopen()/fgets() — same hook-dodging reasoning as every
    // other read in this directory.
    return rawio::rawScanLines(OBF("/proc/cpuinfo"), [](const char* line) {
        if (strncmp(line, OBF("Hardware"), 8) != 0 && strncmp(line, OBF("Features"), 8) != 0) return false;
        return iStrstr(line, OBF("goldfish")) || iStrstr(line, OBF("ranchu")) || iStrstr(line, OBF("virtio")) ||
               iStrstr(line, OBF("qemu"));
    });
}

enum class Verdict { Clean, Suspicious };

// Cheap (a handful of property reads + access() calls + one small file
// scan, well under 1ms) — callers poll this at a longer interval than
// antidbg::check() (device identity itself doesn't change mid-session, but
// a Magisk prop-hider module can be toggled live without restarting the
// process — see the periodic call sites in cheat.cpp).
PCODE inline Verdict check() {
    if (suspiciousBuildProps()) return Verdict::Suspicious;
    if (suspiciousFiles()) return Verdict::Suspicious;
    if (suspiciousCpuinfo()) return Verdict::Suspicious;
    return Verdict::Clean;
}

} // namespace antivm
