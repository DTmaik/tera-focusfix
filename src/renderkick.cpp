// renderkick.cpp - stop ~1.4 million redundant SetEvent syscalls per second
//
// UE3's FRingBuffer::AllocationContext::Commit calls GRenderCommandBuffer.DataWrittenEvent->Trigger()
// after EVERY render command the game thread enqueues (thousands per frame). Each Trigger is a
// SetEvent syscall (plus a WOW64 transition), and almost all of them hit an event that is already
// signalled. The render thread looks at the buffer before it waits, so those calls are pure waste.
//
// Only this one event object gets a private copy of the FEventWin vtable:
//   Trigger : skip the syscall while we know the event is still signalled (g_pending == 1)
//   Wait    : after ANY return (signalled or timed out) the event is known to be non-signalled,
//             so g_pending is cleared. Both wait methods (with timeout and INFINITE) are covered.
//   Reset / Pulse : clear g_pending, then forward.
// Clearing g_pending is always safe (worst case: one extra SetEvent), so a wake-up can never be
// lost: g_pending == 1 always implies the kernel event is signalled or about to be.
//
// 32-bit P31 client only (the 64-bit client has no such SetEvent storm).
#if defined(_M_IX86)
#include "common.h"
#include "client_p31.h"
#include "perf.h"
#include <string.h>

namespace {

typedef void(__fastcall *Method0_t)(void *self, void *edx);
typedef int(__fastcall *WaitMs_t)(void *self, void *edx, DWORD ms);

void **g_origVt;
void *g_vt[16];
volatile LONG g_pending;
volatile LONG g_on = 1;
volatile LONG g_skipped, g_fired;

void __fastcall evTrigger(void *self, void *) {
    if (g_on && InterlockedExchange(&g_pending, 1)) { InterlockedIncrement(&g_skipped); return; }
    InterlockedIncrement(&g_fired);
    ((Method0_t)g_origVt[p31::kEvTrigger])(self, nullptr);
}
void __fastcall evReset(void *self, void *) {
    g_pending = 0;
    ((Method0_t)g_origVt[p31::kEvReset])(self, nullptr);
}
void __fastcall evPulse(void *self, void *) {
    g_pending = 0;
    ((Method0_t)g_origVt[p31::kEvPulse])(self, nullptr);
}
int __fastcall evWaitMs(void *self, void *, DWORD ms) {
    int r = ((WaitMs_t)g_origVt[p31::kEvWaitMs])(self, nullptr, ms);
    g_pending = 0;
    return r;
}
void __fastcall evWaitInf(void *self, void *) {
    ((Method0_t)g_origVt[p31::kEvWaitInf])(self, nullptr);
    g_pending = 0;
}

} // namespace

PatchResult renderkick_install() {
    using namespace p31;
    if (!mem_matches(kCommitFn, kCommitSig, sizeof kCommitSig)) return kPatchNotReady; // may still be packed
    if (!mem_readable(kFEventWinVtbl, sizeof kEvSlotExpected)) return kPatchNotReady;
    for (int i = kEvTrigger; i <= kEvWaitInf; i++)
        if (((DWORD *)kFEventWinVtbl)[i] != kEvSlotExpected[i]) return kPatchMismatch;
    if (!mem_readable(kRenderRingEvent, 4)) return kPatchNotReady;
    void ***ev = *(void ****)kRenderRingEvent;
    if (!ev) return kPatchNotReady;                       // created on the first render command
    if ((DWORD)*ev != kFEventWinVtbl) return kPatchMismatch; // unexpected object, or already patched

    g_origVt = *ev;
    memcpy(g_vt, g_origVt, sizeof g_vt);
    g_vt[kEvTrigger] = (void *)evTrigger;
    g_vt[kEvReset] = (void *)evReset;
    g_vt[kEvPulse] = (void *)evPulse;
    g_vt[kEvWaitMs] = (void *)evWaitMs;
    g_vt[kEvWaitInf] = (void *)evWaitInf;
    g_pending = 0;
    InterlockedExchange((LONG *)ev, (LONG)(void *)g_vt);  // only this object's vtable pointer changes
    return kPatchApplied;
}

void renderkick_enable(bool on) { g_on = on; }
bool renderkick_enabled() { return g_on != 0; }
void renderkick_stats(LONG *syscalls, LONG *skipped) { *syscalls = g_fired; *skipped = g_skipped; }
#endif
