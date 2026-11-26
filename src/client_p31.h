// client_p31.h - addresses inside the 32-bit TERA client this DLL was built for
//   TERA.exe 2.0.1.1, "LIVE-31.04 EME #44", image base 0x400000 (no ASLR)
//
// Every patch verifies the bytes/values below before touching anything. On a different
// client build nothing is patched and focusfix.log says "client build differs".
#pragma once
#include <windows.h>

namespace p31 {

// ---- render command ring buffer (renderkick) -------------------------------------------
// FRingBuffer::AllocationContext::Commit - ends with GRenderCommandBuffer.DataWrittenEvent->Trigger()
constexpr DWORD kCommitFn = 0x0065b6b0;
constexpr BYTE kCommitSig[] = {0x56, 0x8b, 0xf1, 0x83, 0x7e, 0x04, 0x00, 0x74, 0x3f,
                               0x8b, 0x16, 0x8b, 0x46, 0x08, 0x89, 0x42, 0x08};
constexpr DWORD kRenderRingBuffer = 0x02135078;              // GRenderCommandBuffer
constexpr DWORD kRenderRingEvent = kRenderRingBuffer + 0x18; // FEvent* DataWrittenEvent (lazily created)

// FEventWin vtable (thiscall methods, event handle at this+4)
constexpr DWORD kFEventWinVtbl = 0x01c54a98;
constexpr int kEvTrigger = 2;  // 0x6a3080  SetEvent
constexpr int kEvReset = 3;    // 0x695500  ResetEvent
constexpr int kEvPulse = 4;    // 0x68f9d0  PulseEvent
constexpr int kEvWaitMs = 5;   // 0x6a3510  WaitForSingleObject(h, ms) != 0 ? 0 : 1   (retn 4)
constexpr int kEvWaitInf = 6;  // 0x68b050  WaitForSingleObject(h, INFINITE)
constexpr DWORD kEvSlotExpected[] = {0, 0, 0x006a3080, 0x00695500, 0x0068f9d0, 0x006a3510, 0x0068b050};

// ---- garbage collection interval (gc_interval) -----------------------------------------
constexpr DWORD kGEnginePtr = 0x02137a7c;  // UEngine* GEngine
constexpr DWORD kGcIntervalOfs = 0x314;    // UEngine::TimeBetweenPurgingPendingKillObjects (float)
// UWorld::Tick: if (World->TimeSinceLastPendingKillPurge(+0x184) > GEngine->TimeBetween...) PerformGarbageCollection()
constexpr DWORD kGcCompareSite = 0x00c5dbe9;
constexpr BYTE kGcCompareSig[] = {0xa1, 0x7c, 0x7a, 0x13, 0x02,                    // mov eax, [GEngine]
                                  0xf3, 0x0f, 0x10, 0x87, 0x84, 0x01, 0x00, 0x00,  // movss xmm0, [edi+184h]
                                  0xf3, 0x0f, 0x10, 0x88, 0x14, 0x03, 0x00, 0x00}; // movss xmm1, [eax+314h]

} // namespace p31
