// log.cpp - logging and small path helpers
#include "common.h"
#include <stdio.h>
#include <stdarg.h>

HMODULE g_self;
static FILE *g_log;
static CRITICAL_SECTION g_logCs;
static LARGE_INTEGER g_qpf;

double now_ms() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart * 1000.0 / g_qpf.QuadPart;
}

void self_path(wchar_t *out, const wchar_t *file) {
    GetModuleFileNameW(g_self, out, MAX_PATH);
    wchar_t *s = wcsrchr(out, L'\\');
    if (s) s[1] = 0; else out[0] = 0;
    wcscat_s(out, MAX_PATH, file);
}

bool flag_file(const wchar_t *file) {
    wchar_t p[MAX_PATH];
    self_path(p, file);
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

void log_open() {
    InitializeCriticalSection(&g_logCs);
    QueryPerformanceFrequency(&g_qpf);
    wchar_t p[MAX_PATH];
    self_path(p, L"focusfix.log");
    g_log = _wfopen(p, L"w");
}

void logf(const char *fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    EnterCriticalSection(&g_logCs);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logCs);
}
