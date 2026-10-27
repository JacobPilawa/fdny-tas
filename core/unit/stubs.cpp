// The core services the code under test calls, for the host program. log keeps every
// line in memory (unit::LogContains).
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../src/log.h"
#include "unit.h"

namespace {
char g_lines[1 << 20];
int g_len = 0;

// "<tag><text>" as one line; memory keeps what fits.
void Add(const char* level, const char* tag, const char* text) {
    char line[2048];
    int n = snprintf(line, sizeof(line), "[%s] %s%s\n", level, tag, text);
    if (n < 0) return;
    if (n >= static_cast<int>(sizeof(line))) n = sizeof(line) - 1;
    if (g_len + n < static_cast<int>(sizeof(g_lines))) memcpy(g_lines + g_len, line, n), g_len += n, g_lines[g_len] = 0;
}

void Format(const char* level, const char* fmt, va_list ap) {
    char body[1536];
    vsnprintf(body, sizeof(body), fmt, ap);
    Add(level, "", body);
}
} // namespace

namespace unit {
bool LogContains(const char* text) { return strstr(g_lines, text) != nullptr; }
int LogCount(const char* text) {
    int n = 0;
    for (const char* p = strstr(g_lines, text); p; p = strstr(p + 1, text)) ++n;
    return n;
}
void ClearLog() { g_len = 0, g_lines[0] = 0; }
} // namespace unit

namespace fdcore::log {
void Info(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Format("INFO", fmt, ap);
    va_end(ap);
}
void Warn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Format("WARN", fmt, ap);
    va_end(ap);
}
void Error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Format("ERROR", fmt, ap);
    va_end(ap);
}
void Raw(const char* tag, const char* text) { Add("INFO", tag, text); }
} // namespace fdcore::log
