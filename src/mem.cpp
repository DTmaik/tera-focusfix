// mem.cpp - safe memory checks, code and vtable patching
#include "common.h"
#include <string.h>
#include <intrin.h>

bool mem_readable(uintptr_t addr, size_t len) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery((void *)addr, &m, sizeof m) || m.State != MEM_COMMIT) return false;
    if (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return addr + len <= (uintptr_t)m.BaseAddress + m.RegionSize;
}

bool mem_matches(uintptr_t addr, const BYTE *sig, size_t len) {
    return mem_readable(addr, len) && memcmp((void *)addr, sig, len) == 0;
}

bool mem_matches_masked(uintptr_t addr, const BYTE *sig, const char *mask, size_t len) {
    if (!mem_readable(addr, len)) return false;
    const BYTE *p = (const BYTE *)addr;
    for (size_t i = 0; i < len; i++)
        if (mask[i] == 'x' && p[i] != sig[i]) return false;
    return true;
}

// Replaces two bytes of code with one locked compare-exchange, so a thread running through
// that spot sees either the old or the new instruction, never a mix.
bool code_swap16(uintptr_t addr, WORD expected, WORD replacement) {
    DWORD prot;
    if (!VirtualProtect((void *)addr, 2, PAGE_EXECUTE_READWRITE, &prot)) return false;
    WORD prev = (WORD)_InterlockedCompareExchange16((volatile short *)addr, (short)replacement, (short)expected);
    VirtualProtect((void *)addr, 2, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), (void *)addr, 2);
    return prev == expected;
}

void *vtable_swap_slot(void **vtbl, int idx, void *fn) {
    void *old = vtbl[idx];
    if (old == fn) return nullptr;
    DWORD prot;
    if (!VirtualProtect(&vtbl[idx], sizeof(void *), PAGE_READWRITE, &prot)) return nullptr;
    vtbl[idx] = fn;
    VirtualProtect(&vtbl[idx], sizeof(void *), prot, &prot);
    return old;
}
