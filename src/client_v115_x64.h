// client_v115_x64.h - offsets inside the 64-bit TERA client this DLL was built for
//   TERA.osto.exe 1.0.8767, "Live-115.02 GF #8", 64-bit, ASLR -> everything is an RVA
//   relative to the exe's load address (GetModuleHandle(nullptr)).
//
// Every patch verifies the bytes below before touching anything. On a different client build
// nothing is patched and focusfix.log says "client build differs".
#pragma once
#include <windows.h>

namespace v115 {

// ---- garbage collection interval (gc_interval) -----------------------------------------
// UWorld::Tick:  mov rax,[rip+GEngine] / movss xmm0,[rax+7C8h] / comiss xmm0,[r13+260h]
//                (TimeBetweenPurgingPendingKillObjects vs. World->TimeSinceLastPendingKillPurge)
constexpr uintptr_t kGcCompareSite = 0x0070d771;
constexpr BYTE kGcCompareSig[] = {0x48, 0x8b, 0x05, 0, 0, 0, 0,                      // mov rax,[rip+GEngine]
                                  0xf3, 0x0f, 0x10, 0x80, 0xc8, 0x07, 0x00, 0x00,      // movss xmm0,[rax+7C8h]
                                  0x41, 0x0f, 0x2f, 0x85, 0x60, 0x02, 0x00, 0x00};     // comiss xmm0,[r13+260h]
constexpr char kGcCompareMask[] = "xxx????xxxxxxxxxxxxxxxx";
constexpr uintptr_t kGEngineRva = 0x03093120;   // must equal the rip-relative target above
constexpr uintptr_t kGcIntervalOfs = 0x7c8;      // UEngine::TimeBetweenPurgingPendingKillObjects (float)

// ---- diagnostic "recorder" (recorder) ----------------------------------------------------
// Two functions grab a slot in a 64-entry ring of 0x270-byte records and then fill it:
//   mov [rbx+4],rdx / call InitRecord(0x140f700) / mov rcx,[rbx+4] / mov rdx,rax / call CopyRecord(0x1ab4840)
// ~1.2 KB of memset+memcpy per call on the game thread. The patch writes "jmp short +0Fh" over the
// first call, skipping both (the slot is still allocated, just not refilled) - same idea as
// Terarizer's "record-enter" patch for an older 64-bit build.
constexpr uintptr_t kRecorderSites[] = {0x01ab21c7, 0x01ab22bc};
constexpr BYTE kRecorderSig[] = {0x48, 0x89, 0x53, 0x04,             // mov [rbx+4],rdx
                                 0xe8, 0, 0, 0, 0,                   // call InitRecord
                                 0x48, 0x8b, 0x4b, 0x04,             // mov rcx,[rbx+4]
                                 0x48, 0x8b, 0xd0,                   // mov rdx,rax
                                 0xe8, 0, 0, 0, 0};                  // call CopyRecord
constexpr char kRecorderMask[] = "xxxxx????xxxxxxxx????";
constexpr uintptr_t kRecorderInitRva = 0x0140f700;
constexpr uintptr_t kRecorderCopyRva = 0x01ab4840;
constexpr size_t kRecorderCall1 = 4;      // offset of the first call inside the signature
constexpr size_t kRecorderCall2 = 0x10;   // offset of the second call
constexpr WORD kJmpShortSkip = 0x0feb;    // EB 0F: jmp from +4 to +0x15 (past both calls)

} // namespace v115
