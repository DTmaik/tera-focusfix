// recorder.cpp - (64-bit client) stop refilling the diagnostic record ring on every call
//
// See client_v115_x64.h. Each site gets a 2-byte "jmp short" over the InitRecord/CopyRecord
// calls. The original two bytes are kept so the patch can be switched off at runtime.
#if defined(_M_X64)
#include "common.h"
#include "client_v115_x64.h"
#include "perf.h"
#include <stdlib.h>

namespace {

struct Site { uintptr_t addr; WORD original; bool ready; };
Site g_sites[_countof(v115::kRecorderSites)];
bool g_on;

uintptr_t call_target(uintptr_t callAt) { return callAt + 5 + *(const int32_t *)(callAt + 1); }

} // namespace

PatchResult recorder_install() {
    using namespace v115;
    const uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr);
    for (size_t i = 0; i < _countof(kRecorderSites); i++) {
        Site &s = g_sites[i];
        s.addr = base + kRecorderSites[i];
        const uintptr_t call1 = s.addr + kRecorderCall1, call2 = s.addr + kRecorderCall2;
        if (!mem_readable(s.addr, sizeof kRecorderSig)) return kPatchNotReady;
        if (*(const WORD *)call1 == kJmpShortSkip) return kPatchMismatch;  // already patched by someone else
        if (!mem_matches_masked(s.addr, kRecorderSig, kRecorderMask, sizeof kRecorderSig)) return kPatchMismatch;
        if (call_target(call1) != base + kRecorderInitRva || call_target(call2) != base + kRecorderCopyRva) return kPatchMismatch;
        s.original = *(const WORD *)call1;
        s.ready = true;
    }
    return recorder_enable(true) ? kPatchApplied : kPatchMismatch;
}

bool recorder_enable(bool on) {
    bool ok = true;
    for (Site &s : g_sites) {
        if (!s.ready) return false;
        const uintptr_t call1 = s.addr + v115::kRecorderCall1;
        WORD from = on ? s.original : v115::kJmpShortSkip, to = on ? v115::kJmpShortSkip : s.original;
        if (*(const WORD *)call1 == to) continue;
        ok &= code_swap16(call1, from, to);
    }
    if (ok) g_on = on;
    return ok;
}

bool recorder_enabled() { return g_on; }
#endif
