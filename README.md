# tera-focusfix

A drop-in `dinput8.dll` for two TERA clients:

| Build | Client |
|---|---|
| `x86\dinput8.dll` | **32-bit TERA Classic, P31** (`LIVE-31.04`, `TERA.exe` 2.0.1.1) |
| `x64\dinput8.dll` | **64-bit TERA, v115** (`Live-115.02`, `TERA.osto.exe` 1.0.8767) |

What it does:

1. **Removes the freeze when you alt-tab back into the game** (windowed / borderless). Both clients.
2. **More FPS in heavy scenes**: `renderkick` on the 32-bit client (up to ~10%), the `recorder`
   patch on the 64-bit client (an open replacement for Terarizer's main-thread patch).
3. **Makes the periodic garbage-collection stutter 5× rarer**. Both clients.

`TERA.exe` is never modified on disk.

---

## What it does

### 1. Alt-tab freeze fix (both clients)
About one second after the game window regains focus, TERA looks for gamepads
(`IDirectInput8::EnumDevices(DI8DEVCLASS_GAMECTRL)` plus `GetDeviceStatus` for 4 gamepad slots),
on the game thread. To answer that, Windows opens **every HID device** in the system. A single slow
device (on the test PC: a keyboard's vendor HID interface) freezes the whole game.

The DLL caches the device list and the failed status queries, so later calls return instantly.
When Windows reports that a device was plugged in or removed, the caches are dropped, so gamepad
hot-plugging still works.

| | before | after |
|---|---|---|
| 32-bit client: freeze after tabbing back in | 650–715 ms | none (longest frame ~30 ms) |
| 64-bit client: gamepad search on re-activation | 454 ms | 0.00 ms (from cache) |

### 2a. renderkick (32-bit client): up to ~10% more FPS in heavy scenes
The engine's render-command queue (`FRingBuffer::AllocationContext::Commit`) wakes the render
thread with a `SetEvent` system call **after every single render command**: about 1.4 million times
per second, almost always on an event that is already signaled. The DLL gives only that one event
object its own vtable, so redundant wake-ups are skipped. The render thread checks the queue before
it sleeps anyway, and every time it returns from waiting the state is reset, so no wake-up can
get lost.

Measured: 99.99% of those syscalls removed. How much FPS that gives depends on the game thread's load,
because the saved time only shows up when the game thread is the bottleneck:

| Scene (alternating ON/OFF A/B test) | FPS without → with | Gain |
|---|---|---|
| Dungeon boss fight (usually dips to 60–70 FPS) | 149 → 165 avg | **+10.5%** |
| Busy open-world area | 148 → 151 | +2% (within noise) |
| Light scene | 223 → 227 | +1.6% (within noise) |

So it helps most where FPS drops the most, and it never costs FPS. (The 64-bit client has no such
syscall storm, so this patch is 32-bit only.)

### 2b. recorder (64-bit client): less work on the game thread
Two engine functions log into a 64-slot diagnostic ring buffer, and on every call they build an
empty 624-byte record and copy it into the slot (~1.2 KB of memory work per call). A 2-byte
`jmp short` skips the fill and copy; the slot is still taken, just not refilled. The 2-byte write is
atomic (hot-patch style) and can be switched off live with `recorder.off`.

Measured: the recorder's share of game-thread samples drops from **5.5% to 1.2%**. This is the
same kind of patch as Terarizer's "record-enter", re-found for the v115 build, because Terarizer's
fixed addresses don't match it.

### 3. gc_interval (both clients): rarer GC stutter
Every 60 s the engine runs a garbage collection, which costs one long frame (22–60 ms instead of
~5 ms on the 32-bit client). The DLL raises `TimeBetweenPurgingPendingKillObjects` at runtime to
300 s (configurable). Only a value in memory changes, no code.

---

## Install
1. Download the ZIP for your client from [Releases](../../releases/latest)
   (`tera-focusfix-x86.zip` for the 32-bit P31 client, `tera-focusfix-x64.zip` for the 64-bit v115
   client), or build it yourself (see [Build](#build)).
2. Copy the `dinput8.dll` from it into the client's `Binaries` folder (next to the game exe).
3. Start the game normally. A log is written to `Binaries\focusfix.log`.

Uninstall: delete `dinput8.dll`.

If the `Binaries` folder already contains a `dinput8.dll` from your server (some servers ship their
own client patch under that name), don't overwrite it: this DLL does not chain-load other
`dinput8.dll` patches yet.

The DLL forwards every DirectInput call to the real system `dinput8.dll`. All game addresses are
checked against the expected bytes before anything is changed; with a different client build the
performance patches simply stay off (the log says `client build differs`) and only the alt-tab fix
runs.

## Options
Empty files (or a value) in the `Binaries` folder:

| File | Effect |
|---|---|
| `focusfix.off` | disable the DirectInput caches (alt-tab fix) |
| `perf.off` | disable all performance patches |
| `renderkick.off` | 32-bit: disable renderkick, toggled live (handy for A/B comparisons) |
| `recorder.off` | 64-bit: disable the recorder patch, toggled live |
| `gc_interval.value` | GC interval in seconds (30–3600, default 300) |

A healthy log looks like (64-bit shown; the 32-bit one says `renderkick installed`):
```
tera-focusfix loaded (64-bit, proxy mode)
perf: recorder installed (diagnostic record ring is no longer refilled on every call)
perf: gc_interval 60s -> 300s
dinput: hooks EnumA=ok EnumW=ok StatusA=ok StatusW=ok CreateA=ok CreateW=ok, cache ON
dinput: EnumDevicesW(type=4 flags=0x1) -> 0 devices from cache, 0.00 ms
```

## Build
Requires Visual Studio 2019/2022 with the C++ x86/x64 tools. Run `build.cmd`; it builds both:

```
build\x86\dinput8.dll           32-bit P31 client
build\x64\dinput8.dll           64-bit v115 client
build\<arch>\tools\inject.exe   load a DLL into a running client (testing without restart)
build\<arch>\tools\proxytest.exe  loads dinput8.dll from its own folder and enumerates twice
build\<arch>\tools\devtime.exe  times CreateDevice per input device (finds the slow one)
build\x86\tools\tsampler.exe    samples the 32-bit client's thread stacks around alt-tab
build\x86\tools\diag.dll        counts SetEvent/WaitForSingleObject calls per call site (injected)
```
`tools\scripts\` holds the Python measurement scripts (FPS via RivaTuner shared memory, A/B
benchmark, GC detection, frame-time spikes, memory dump, 64-bit profiler).

## Layout
```
src/
  dllmain.cpp         entry point, dinput8 proxy exports, proxy vs injected mode
  dinput_cache.cpp    alt-tab fix (EnumDevices / GetDeviceStatus caches)
  renderkick.cpp      32-bit: render-thread wake-up deduplication
  recorder.cpp        64-bit: skip the diagnostic record fill
  gc_interval.cpp     GC interval (both)
  perf.cpp            background thread applying/maintaining the performance patches
  client_p31.h        32-bit client addresses + byte signatures
  client_v115_x64.h   64-bit client RVAs + byte signatures
  log.cpp, mem.cpp    logging, memory and code-patch helpers
  dinput8.def         export table
tools/                diagnostics used to find all of this
docs/ANALYSIS.md      how the problems were found, with measurements
```

## Anti-tamper protection is not touched
tera-focusfix does **not** disable, bypass or patch the client's anti-tampering / integrity checks.
Everything it changes is listed above: DirectInput vtables, one event object's vtable (32-bit),
one engine setting (both), and the 2-byte recorder jump (64-bit, which the v115 client tolerates
as-is). If a patch would need the protection switched off on some client build, that patch is
simply not offered for that build.

## Comparison with "Terarizer"
Terarizer is a closed-source 64-bit performance patch with fixed addresses for one specific 64-bit
client build (Patch 100.02). Its addresses match neither the 32-bit client nor the v115 client.
This project re-finds the useful parts per client: GC interval and the record-fill patch for v115.

One important difference: besides the advertised patches, Terarizer also **disables the client's
anti-tampering mechanism** (its "secfix" part). The release post doesn't mention this; it only
talks about "keeping the client stable". tera-focusfix deliberately leaves that out (see above).
See [docs/ANALYSIS.md](docs/ANALYSIS.md#terarizer) for a patch-by-patch comparison.
