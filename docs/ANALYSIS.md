# Analysis notes

Client: `TERA.exe` 2.0.1.1, `LIVE-31.04 EME #44`, 32-bit, image base `0x400000` (no ASLR), packed
(the static import table only lists KERNEL32; DLLs are resolved by the unpacker with the normal search
order, which is why a `dinput8.dll` next to the exe is picked up). Renderer: Direct3D 9.
All addresses below are virtual addresses in the running (unpacked) process.

## 1. Alt-tab freeze

**Measurement** (`tools/sampler/tsampler.c`): switch focus to the game automatically, then sample
all thread stacks every 4 ms for 3–4 s, probe the window's message pump with `WM_NULL` round trips,
and hash a screen region to detect visible frame changes.

- 4/4 activations: message pump blocked and no visible frame for **650–715 ms**, starting ~1 s
  after activation (TERA polls on a 1 s tick).
- 90% of main-thread samples in that window were inside `DINPUT8.dll`, mostly registry
  (`NtOpenKeyEx`, `NtQueryKey`) and `DeviceIoControl` on HID devices, i.e. DirectInput rebuilding
  its HID device list.
- Logging the hooked calls showed the exact sequence per activation:
  `GetDeviceStatusW(GUID_NULL)` ×4 (~12 ms each, `REGDB_E_CLASSNOTREG`) and
  `EnumDevicesW(DI8DEVCLASS_GAMECTRL, DIEDFL_ATTACHEDONLY)` (~620 ms).
- `tools/devtime` (times `CreateDevice` for every attached device): one keyboard HID interface
  took **~650 ms** to open; everything else 0–20 ms.

**Fix:** cache `EnumDevices` results per (type, flags) and failed `GetDeviceStatus` results per GUID,
invalidated by `CM_Register_Notification` (all device-interface classes). The vtables of the
system `dinput8.dll` are patched (they are shared by all instances), so this also works when the
DLL is injected after the game created its DirectInput object.

## 2. renderkick

**Finding:** in steady state the main thread spent ~23% of its samples in `NtSetEvent`.
`tools/diag/diag.c` (redirects TERA's `SetEvent`/`WaitForSingleObject` import slots for 6 s and
counts per call site) reported **~1.35 million `SetEvent` calls per second**, all on the game thread.

All call sites go through `0x65b6b0` = `FRingBuffer::AllocationContext::Commit`:

```
65b6b0  push esi / mov esi,ecx / cmp [esi+4],0 / je ...
        ... WritePointer = AllocationEnd ...
        lazily create DataWrittenEvent (GSynchronizeFactory->CreateSynchEvent(false,false))
65b6f5  jmp [eax+8]        ; DataWrittenEvent->Trigger()  -> SetEvent
```

So every render command (thousands per frame) triggers the render thread's event.
`GRenderCommandBuffer` = `0x2135078`, event pointer at `+0x18`.

`FEventWin` vtable `0x1c54a98` (thiscall, handle at `this+4`):

| slot | address | method |
|---|---|---|
| 2 | `0x6a3080` | Trigger → `SetEvent` |
| 3 | `0x695500` | Reset → `ResetEvent` |
| 4 | `0x68f9d0` | Pulse → `PulseEvent` |
| 5 | `0x6a3510` | Wait(ms) → `WaitForSingleObject(h, ms) == 0` (`retn 4`) |
| 6 | `0x68b050` | Wait() → `WaitForSingleObject(h, INFINITE)` |

The render thread (`0x8f004d`) calls `BeginRead` first and only waits (`WaitForRead(16 ms)`,
slot 5) when the queue is empty.

**Fix:** only that event object gets a copy of the vtable. Trigger skips the syscall while a flag
says the event is still signaled; Wait (both variants), Reset and Pulse clear the flag on return.
Clearing is always safe (it can only cause an extra `SetEvent`), so a wake-up cannot be lost.

> v2.0 only cleared the flag after a *successful* slot-5 wait. When the event was consumed through
> slot 6 during loading, the flag stayed set and the render thread fell back to its 16 ms timeout
> (log showed 0 real wake-ups per minute; A/B −3.6%). Fixed by clearing after every wait return and
> hooking slot 6.

**Result:** ~88 million skipped calls per minute. FPS (alternating ON/OFF pairs, RTSS):
dungeon boss fight +10.5% (4 pairs, ON faster in all of them); busy open world +2.1% (5 pairs);
light scene +1.6% (4 pairs). The last two are within the noise. The gain scales with the number of
render commands per frame, and it only shows up when the game thread, not the render thread, limits
the frame rate.

**Bottleneck context:** CPU over 8 s: render thread 87%, NVIDIA driver thread 80%, main thread 74%
(23% of it waiting for the render thread). The client is render-thread / draw-call bound.

## 3. GC interval

`GEngine` = `[0x2137a7c]`; scanning it for `60.0f` gives exactly one hit, `+0x314`. Confirmed in
`UWorld::Tick`:

```
c5dbcb  World->TimeSinceLastPendingKillPurge (+0x184) += DeltaSeconds
c5dbe0  call IsAsyncLoading
c5dbe9  mov eax,[GEngine] / movss xmm0,[edi+184h] / movss xmm1,[eax+314h] / comiss / jbe
c5dc0e  call 0xc47d20      ; PerformGarbageCollection
```
(`0xc3cf80` = ForceGarbageCollection sets the timer to `interval + 1`.)

`tools/scripts/gcmon.py` watches `[0x2133798]+0x184` for resets: GC every 60 s, worst frame around
each GC 22–60 ms (median frame ~5 ms). After the patch: no GC at 60 s, timer keeps counting.

The INI (`S1Engine.ini`, `TimeBetweenPurgingPendingKillObjects=60`) is rewritten by the game at
start, so the value is set at runtime instead.

## 4. 64-bit v115 client

Client: `TERA.osto.exe` 1.0.8767, `Live-115.02 GF #8`, x64, ASLR, WinLicense-protected (code is only
decrypted in memory). It also contains the server's own patch as extra `.osto*` sections. All
offsets below are RVAs. Analysis was done on a memory dump of the running client
(`tools/scripts/dumpimg.py`), profiling with a 64-bit leaf-RIP + return-address sampler.

**Profile (game thread, 8 s, light scene):** ~60% in `NtDelayExecution` (UE3 frame limiter spin,
`bSmoothFrameRate=TRUE`, `MaxSmoothedFrameRate=300`), only 1.7% in `NtSetEvent`, so there is no
SetEvent storm and renderkick is not needed.

**Recorder:** searching for Terarizer's anchor (`48 89 53 04` + `call` + 7 bytes + `call`) found two
functions with the same body, `0x1ab2110` (2 callers) and `0x1ab2200` (8 callers):

```
lock xadd [counter],1 ; slot = counter % 64          ; 64-entry ring at .data, 0x270 bytes each
mov  [rbx],eax
mov  [rbx+4],rdx      ; rdx = &ring[slot]
call 0x140f700        ; build a default 0x270-byte record on the stack (zeros / -1)
mov  rcx,[rbx+4]
mov  rdx,rax
call 0x1ab4840        ; copy the 0x270 bytes into the ring slot
```

The patch writes `EB 0F` (jmp short to after the second call) over the first `call`, with one
locked 16-bit compare-exchange. Result: recorder share of game-thread samples 5.50% → 1.17% (what is
left is the slot allocation itself). WinLicense did not react; the patch was still in place after
minutes of play.

**GC interval:** searching for `mov reg,[rip+G]` followed by `movss/comiss xmm,[reg+off]` and
checking the live value for 60.0 gives exactly one hit: `GEngine` at `0x3093120`, field `+0x7c8`
(the same offset Terarizer uses). Confirmed in `UWorld::Tick`:

```
70d771  mov rax,[rip+GEngine] / movss xmm0,[rax+7C8h] / comiss xmm0,[r13+260h] / jae
        ; World->TimeSinceLastPendingKillPurge = +0x260
70d792  call 0x7043c0     ; PerformGarbageCollection
```
`GWorld` = `0x3097708`. After the patch the GC timer runs past 60 s.

**Alt-tab:** the v115 client also calls `EnumDevicesW(DI8DEVCLASS_GAMECTRL)` on re-activation; first
real call 454 ms on the test PC, then 0.00 ms from the cache.

## Terarizer

Terarizer is a closed-source 64-bit `dinput8.dll` (MinGW). Reversed from its code, every patch uses
fixed RVAs of one specific 64-bit build. Checked against two 64-bit clients: on a P100 client
(Asura, `Live-100.02`) only the GC function prologue and the `GEngine` RVA still matched; on the
v115 client nothing matched. So Terarizer does nothing on either of them.

| patch | what it does | 32-bit P31 | 64-bit v115 |
|---|---|---|---|
| `record-enter` | at RVA `0x19ca95c` (`mov [rbx+4],edx`), NOPs the `call`s at `+4` and `+0x10` ("recorder fills NOPped") | no such recorder; the game-thread hog there is the SetEvent storm → **renderkick** | re-found at `0x1ab21c7` / `0x1ab22bc` → **recorder** |
| `footik_offscreen` | RVA `0x14b6b4b`: `mov edx,1` → `mov edx,0` | not ported (~2% of game-thread samples) | not ported yet |
| `secfix` | disables the client's anti-tampering mechanism (not mentioned in its release post) | left out | left out (the recorder patch works without it) |
| `gc_interval` | `GEngine+0x7c8` = 600 s | **gc_interval**, `GEngine+0x314` | **gc_interval**, `GEngine(0x3093120)+0x7c8` |
| `gc_timer` | hooks the GC function to log durations | not ported; `gcmon.py` measures from outside | not ported |
