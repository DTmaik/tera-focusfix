// common.h - shared helpers for tera-focusfix
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

// log.cpp
void log_open();                                   // opens focusfix.log next to the DLL
void logf(const char *fmt, ...);                   // timestamped line in focusfix.log
void self_path(wchar_t *out, const wchar_t *file); // <dir of this DLL>\file
bool flag_file(const wchar_t *file);               // true if <dll dir>\file exists
double now_ms();
extern HMODULE g_self;

// mem.cpp
bool mem_readable(uintptr_t addr, size_t len);
bool mem_matches(uintptr_t addr, const BYTE *sig, size_t len);
bool mem_matches_masked(uintptr_t addr, const BYTE *sig, const char *mask, size_t len); // mask: 'x' = compare, '?' = any
bool code_swap16(uintptr_t addr, WORD expected, WORD replacement);                      // atomic 2-byte code patch
void *vtable_swap_slot(void **vtbl, int idx, void *fn);                                 // returns previous entry (nullptr if unchanged)

// dinput_cache.cpp
void dinput_cache_install(HMODULE realDinput8);

// perf.cpp
void perf_start();
