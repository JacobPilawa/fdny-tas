#include "log.h"

#include <windows.h>
#include <stdarg.h>

namespace fdcore::log {
namespace {

HANDLE g_file = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_lock;
bool g_lockInit = false;

void Write(const char* level, const char* fmt, va_list ap) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    char line[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = wsprintfA(line, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] ", t.wYear, t.wMonth,
                      t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, level);
    // wvsprintfA caps at 1024 incl. NUL; leave room for CRLF.
    char body[1024];
    int m = wvsprintfA(body, fmt, ap);
    if (m > static_cast<int>(sizeof(line)) - n - 3) m = static_cast<int>(sizeof(line)) - n - 3;
    for (int i = 0; i < m; ++i) line[n + i] = body[i];
    n += m;
    line[n++] = '\r';
    line[n++] = '\n';
    EnterCriticalSection(&g_lock);
    DWORD wrote = 0;
    WriteFile(g_file, line, static_cast<DWORD>(n), &wrote, nullptr);
    FlushFileBuffers(g_file);
    LeaveCriticalSection(&g_lock);
}

} // namespace

bool Open(const char* path) {
    if (!g_lockInit) {
        InitializeCriticalSection(&g_lock);
        g_lockInit = true;
    }
    g_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    return g_file != INVALID_HANDLE_VALUE;
}

void Close() {
    if (g_file != INVALID_HANDLE_VALUE) CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
}

void Raw(const char* tag, const char* text) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    char head[96];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = wsprintfA(head, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [INFO] %s", t.wYear, t.wMonth, t.wDay, t.wHour,
                      t.wMinute, t.wSecond, t.wMilliseconds, tag);
    EnterCriticalSection(&g_lock);
    DWORD wrote = 0;
    WriteFile(g_file, head, static_cast<DWORD>(n), &wrote, nullptr);
    WriteFile(g_file, text, static_cast<DWORD>(lstrlenA(text)), &wrote, nullptr);
    WriteFile(g_file, "\r\n", 2, &wrote, nullptr);
    FlushFileBuffers(g_file);
    LeaveCriticalSection(&g_lock);
}

void Info(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Write("INFO", fmt, ap);
    va_end(ap);
}

void Warn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Write("WARN", fmt, ap);
    va_end(ap);
}

void Error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Write("ERROR", fmt, ap);
    va_end(ap);
}

} // namespace fdcore::log
