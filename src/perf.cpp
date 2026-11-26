// perf.cpp - background thread that applies and maintains the performance patches
//
// The clients are packed; right after start their code may not be unpacked yet and the objects
// we need (render event, GEngine) may not exist. So every patch is retried for up to 3 minutes.
// Afterwards the thread handles the runtime switches, re-applies the GC interval if a config
// reload reset it, and (32-bit) logs renderkick statistics once a minute.
//
//   32-bit P31 client : renderkick + gc_interval
//   64-bit v115 client: recorder   + gc_interval
#include "common.h"
#include "perf.h"

namespace {

const int kTickMs = 100;
const int kStartupTicks = 1800;  // 3 minutes

// One optional code/data patch with a runtime on/off file.
struct Feature {
    const char *name;
    const wchar_t *offFile;
    PatchResult (*install)();
    bool (*setEnabled)(bool);
    bool (*isEnabled)();
    const char *installedMsg;
    bool active = false, done = false;

    void try_install(int tick) {
        if (done) return;
        PatchResult r = install();
        if (r == kPatchApplied) { active = done = true; logf("perf: %s installed (%s)", name, installedMsg); }
        else if (r == kPatchMismatch || tick >= kStartupTicks) {
            done = true;
            logf("perf: %s NOT installed (%s)", name, r == kPatchMismatch ? "client build differs or already patched" : "client did not become ready in time");
        }
    }
    void follow_switch() {
        if (!active) return;
        bool want = !flag_file(offFile);
        if (want != isEnabled() && setEnabled(want)) logf("perf: %s %s", name, want ? "ON" : "OFF (off-file present)");
    }
};

#if defined(_M_IX86)
bool rk_set(bool on) { renderkick_enable(on); return true; }
Feature g_feature = {"renderkick", L"renderkick.off", renderkick_install, rk_set, renderkick_enabled,
                     "redundant render-thread wake-ups are skipped"};
#else
Feature g_feature = {"recorder", L"recorder.off", recorder_install, recorder_enable, recorder_enabled,
                     "diagnostic record ring is no longer refilled on every call"};
#endif

DWORD WINAPI perf_thread(LPVOID) {
    if (flag_file(L"perf.off")) { logf("perf: perf.off present - no performance patches"); return 0; }

    const float gcWant = gc_interval_setting();
    PatchResult gc = kPatchNotReady;
    bool gcFailLogged = false;
#if defined(_M_IX86)
    LONG lastFired = 0, lastSkipped = 0;
    DWORD statsT0 = GetTickCount();
#endif

    for (int tick = 0;; tick++) {
        Sleep(kTickMs);
        g_feature.try_install(tick);

        if (gc == kPatchNotReady || (gc == kPatchApplied && tick % 100 == 0)) {   // re-check every 10 s
            gc = gc_interval_apply(gcWant);
            if (gc == kPatchMismatch && tick < kStartupTicks) gc = kPatchNotReady;  // code may still be packed
            else if (gc == kPatchMismatch && !gcFailLogged) { gcFailLogged = true; logf("perf: gc_interval NOT applied (client build differs)"); }
        }

        if (tick % 5) continue;
        g_feature.follow_switch();
#if defined(_M_IX86)
        if (g_feature.active && GetTickCount() - statsT0 >= 60000) {
            LONG fired, skipped;
            renderkick_stats(&fired, &skipped);
            logf("perf: last 60s render wake-ups: %ld syscalls, %ld skipped", fired - lastFired, skipped - lastSkipped);
            lastFired = fired; lastSkipped = skipped; statsT0 = GetTickCount();
        }
#endif
    }
}

} // namespace

void perf_start() { CloseHandle(CreateThread(nullptr, 0, perf_thread, nullptr, 0, nullptr)); }
