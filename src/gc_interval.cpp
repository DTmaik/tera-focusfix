// gc_interval.cpp - run UE3's garbage collection less often
//
// UWorld::Tick runs PerformGarbageCollection whenever TimeSinceLastPendingKillPurge exceeds
// GEngine->TimeBetweenPurgingPendingKillObjects (60 s from S1Engine.ini). Every collection
// costs one long frame (measured 22-60 ms vs ~5 ms on the 32-bit client). The value lives in the
// engine object, so we simply set it at runtime - no code is patched. The INI is rewritten by the
// game, which is why it is not edited there.
#include "common.h"
#include "perf.h"
#include <stdio.h>
#if defined(_M_X64)
#include "client_v115_x64.h"
#else
#include "client_p31.h"
#endif

float gc_interval_setting() {
    float v = 300.0f; // 5 minutes (Terarizer used 600 s; 300 s keeps memory growth moderate)
    wchar_t p[MAX_PATH];
    self_path(p, L"gc_interval.value");
    if (FILE *f = _wfopen(p, L"r")) {
        double d;
        if (fscanf(f, "%lf", &d) == 1) v = (float)d;
        fclose(f);
    }
    if (v < 30) v = 30;
    if (v > 3600) v = 3600;
    return v;
}

// Address of the float, or 0 (not ready) / UINTPTR_MAX (client mismatch)
static uintptr_t gc_field() {
#if defined(_M_X64)
    using namespace v115;
    const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    const uintptr_t site = base + kGcCompareSite;
    if (!mem_matches_masked(site, kGcCompareSig, kGcCompareMask, sizeof kGcCompareSig)) return UINTPTR_MAX;
    const uintptr_t global = site + 7 + *(const int32_t *)(site + 3);   // rip-relative GEngine
    if (global != base + kGEngineRva) return UINTPTR_MAX;
    const uintptr_t engine = *(const uintptr_t *)global;
#else
    using namespace p31;
    if (!mem_matches(kGcCompareSite, kGcCompareSig, sizeof kGcCompareSig)) return UINTPTR_MAX;
    const uintptr_t engine = *(const DWORD *)kGEnginePtr;
#endif
    if (!engine || !mem_readable(engine + kGcIntervalOfs, 4)) return 0;
    return engine + kGcIntervalOfs;
}

PatchResult gc_interval_apply(float want) {
    const uintptr_t addr = gc_field();
    if (addr == UINTPTR_MAX) return kPatchMismatch;
    if (!addr) return kPatchNotReady;
    float *field = (float *)addr;
    const float cur = *field;
    if (cur == want) return kPatchApplied;
    if (!(cur >= 1.0f && cur <= 86400.0f)) return kPatchMismatch; // not the kind of value expected there
    *field = want;
    logf("perf: gc_interval %.0fs -> %.0fs", cur, want);
    return kPatchApplied;
}
