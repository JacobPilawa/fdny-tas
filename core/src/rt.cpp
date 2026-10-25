// Tests: tests/core_loads.yaml tests/core_release.yaml
// Freestanding runtime shims: the core links no CRT (kernel32/user32/bcrypt
// only, like asi/FDNYDeterministic), so the compiler's implicit helpers live here.
#include <stddef.h>

extern "C" {
int _fltused = 0;

void* memcpy(void* d, const void* s, size_t n) {
    auto* dp = static_cast<unsigned char*>(d);
    auto* sp = static_cast<const unsigned char*>(s);
    while (n--) *dp++ = *sp++;
    return d;
}

void* memset(void* d, int c, size_t n) {
    auto* dp = static_cast<unsigned char*>(d);
    while (n--) *dp++ = static_cast<unsigned char>(c);
    return d;
}

void* memmove(void* d, const void* s, size_t n) {
    auto* dp = static_cast<unsigned char*>(d);
    auto* sp = static_cast<const unsigned char*>(s);
    if (dp < sp) {
        while (n--) *dp++ = *sp++;
    } else {
        dp += n;
        sp += n;
        while (n--) *--dp = *--sp;
    }
    return d;
}

int memcmp(const void* a, const void* b, size_t n) {
    auto* ap = static_cast<const unsigned char*>(a);
    auto* bp = static_cast<const unsigned char*>(b);
    for (; n; --n, ++ap, ++bp)
        if (*ap != *bp) return *ap < *bp ? -1 : 1;
    return 0;
}

size_t strlen(const char* s) {
    const char* p = s;
    while (*p) ++p;
    return static_cast<size_t>(p - s);
}
}
