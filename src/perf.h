// perf.h - performance patches (addresses in client_p31.h / client_v115_x64.h)
#pragma once
#include <windows.h>

// Result of an install/apply attempt
enum PatchResult { kPatchNotReady = 0, kPatchApplied = 1, kPatchMismatch = -1 };

// gc_interval.cpp (both clients)
float gc_interval_setting();             // from gc_interval.value, default 300 s
PatchResult gc_interval_apply(float seconds);

#if defined(_M_IX86)
// renderkick.cpp (32-bit P31 client)
PatchResult renderkick_install();
void renderkick_enable(bool on);
bool renderkick_enabled();
void renderkick_stats(LONG *syscalls, LONG *skipped);
#endif

#if defined(_M_X64)
// recorder.cpp (64-bit v115 client)
PatchResult recorder_install();
bool recorder_enable(bool on);
bool recorder_enabled();
#endif
