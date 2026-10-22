// Debug-only text-drawing benchmark (in-frame text investigation, after item 13).
//   fd_bench_text <calls> <chars> <every> <mode>
// draws <calls> strings of <chars> characters with geEngine_Printf on every
// <every>-th frame. mode: 0 letters, 1 half spaces (box-like), 2 all spaces,
// 3 letters in one call per <calls> lines joined by nothing (single long string).
// fd_bench_text 0 = off. Frame rate is measured by the harness (pipe stats).
#ifdef FDCORE_DEBUG

#include <windows.h>

#include "addr.h"
#include "cmd.h"
#include "console.h"
#include "hooks.h"
#include "log.h"
#include "modules.h"

namespace fdcore::bench {
namespace {

using PrintfFn = int(__cdecl*)(void* engine, int x, int y, const char* fmt, ...);
PrintfFn g_printf = nullptr;
int g_calls = 0, g_chars = 0, g_every = 1, g_mode = 0;
unsigned long g_frame = 0;
char g_line[1024];

int Num(const char* s) {
    int v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v;
}

void Cmd(int argc, const char* const* argv) {
    g_calls = argc > 1 ? Num(argv[1]) : 0;
    g_chars = argc > 2 ? Num(argv[2]) : 65;
    g_every = argc > 3 ? Num(argv[3]) : 1;
    g_mode = argc > 4 ? Num(argv[4]) : 0;
    if (g_every < 1) g_every = 1;
    if (g_chars > 1000) g_chars = 1000;
    for (int i = 0; i < g_chars; ++i) {
        const char c = static_cast<char>('A' + i % 26);
        g_line[i] = g_mode == 2 ? ' ' : (g_mode == 1 && (i / 4) % 2) ? ' ' : c;
    }
    g_line[g_chars] = 0;
    log::Info("bench: text calls=%d chars=%d every=%d mode=%d", g_calls, g_chars, g_every, g_mode);
    console::Print("fd_bench_text %d %d %d %d", g_calls, g_chars, g_every, g_mode);
}

void Draw() {
    ++g_frame;
    if (!g_calls || !g_printf || g_frame % g_every) return;
    void* engine = hooks::CurrentEngine();
    if (!engine) return;
    for (int i = 0; i < g_calls; ++i) g_printf(engine, 8, 60 + (i % 30) * 12, "%s", g_line);
}

// --- fd_bench_blit <n> <w> <h>: n geEngine_DrawBitmap calls per frame of a
// w x h rect of the engine's own font bitmap (the Printf glyph source).
struct GeRect {
    int32_t left, right, top, bottom;
};
using DrawFn = int(__fastcall*)(void* engine, void* bmp, const GeRect* src, uint32_t x, uint32_t y);
DrawFn g_draw = nullptr;
int g_blits = 0, g_bw = 8, g_bh = 8;
bool g_dumped = false;

void DumpFont(uintptr_t engine) {
    uint32_t bmp = 0, glyphA = 0;
    addr::SafeRead(engine + addr::Field(addr::Id::engine_f_font_bitmap), &bmp, 4);
    addr::SafeRead(engine + addr::Field(addr::Id::engine_f_font_glyphs) + 'A' * 4, &glyphA, 4);
    uint32_t b[0xB0 / 4] = {}, th[0x40 / 4] = {};
    addr::SafeRead(bmp, b, sizeof(b));
    if (b[0xAC / 4]) addr::SafeRead(b[0xAC / 4], th, sizeof(th));
    char line[1024];
    int o = wsprintfA(line, "bench: font bmp=%08lx glyph['A']=%08lx |", static_cast<unsigned long>(bmp),
                      static_cast<unsigned long>(glyphA));
    for (int i = 0; i < 0xB0 / 4; ++i) o += wsprintfA(line + o, " %lx", static_cast<unsigned long>(b[i]));
    log::Info("%s", line);
    o = wsprintfA(line, "bench: font thandle:");
    for (int i = 0; i < 0x40 / 4; ++i) o += wsprintfA(line + o, " %lx", static_cast<unsigned long>(th[i]));
    log::Info("%s", line);
}

using FlushFn = int(__cdecl*)(void* engine);
FlushFn g_flush = nullptr;
int g_bx = 0, g_order = 0, g_doFlush = 0;  // flush: 1 = geEngine_FlushPipeline before the blits  // source x in the font bitmap; rect field order 0 = {L,R,T,B}, 1 = {L,T,R,B}

void CmdBlit(int argc, const char* const* argv) {
    g_blits = argc > 1 ? Num(argv[1]) : 0;
    g_bw = argc > 2 ? Num(argv[2]) : 8;
    g_bh = argc > 3 ? Num(argv[3]) : 8;
    g_bx = argc > 4 ? Num(argv[4]) : 0;
    g_order = argc > 5 ? Num(argv[5]) : 0;
    g_doFlush = argc > 6 ? Num(argv[6]) : 0;
    log::Info("bench: blit n=%d w=%d h=%d x=%d order=%d", g_blits, g_bw, g_bh, g_bx, g_order);
}

void Blit() {
    if (!g_blits || !g_draw) return;
    void* engine = hooks::CurrentEngine();
    if (!engine) return;
    const uintptr_t e = reinterpret_cast<uintptr_t>(engine);
    if (!g_dumped) g_dumped = true, DumpFont(e);
    uint32_t bmp = 0;
    addr::SafeRead(e + addr::Field(addr::Id::engine_f_font_bitmap), &bmp, 4);
    if (!bmp) return;
    const GeRect r = g_order ? GeRect{g_bx, 0, g_bx + g_bw - 1, g_bh - 1}   // read as {L,T,R,B}
                             : GeRect{g_bx, g_bx + g_bw - 1, 0, g_bh - 1};  // {L,R,T,B}
    if (g_doFlush && g_flush) g_flush(engine);
    int ok = 0;
    for (int i = 0; i < g_blits; ++i)
        ok += g_draw(engine, reinterpret_cast<void*>(bmp), &r, static_cast<uint32_t>(40 + (i * 37) % 400),
                     static_cast<uint32_t>(60 + (i * 13) % 300));
    static int logged = 0;
    if (logged++ < 2) log::Info("bench: blit %d calls, %d returned TRUE", g_blits, ok);
}

// --- fd_bench_bmp <w> <h> <format> <every> <ck>: our own GDI-rendered bitmap,
// re-rendered + uploaded every <every> frames (a counter changes the text),
// blitted once per frame after geEngine_FlushPipeline. format 13 = XRGB, 5 = 565.
using CreateFn = void*(__fastcall*)(int width, int height, int maxMip, int format);
using LockFn = int(__fastcall*)(void* bmp, void** target, int minMip, int maxMip, int format);
using BmpFn = int(__fastcall*)(void* bmp);
using GetBitsFn = void*(__fastcall*)(void* bmp);
using GetInfoFn = int(__fastcall*)(void* bmp, uint32_t* info, uint32_t* info2);
using AddFn = int(__cdecl*)(void* engine, void* bmp);
using ColorKeyFn = int(__fastcall*)(void* bmp, int haveKey, uint32_t key, int smart);
CreateFn g_create;
LockFn g_lock;
BmpFn g_unlock;
GetBitsFn g_bits;
GetInfoFn g_info;
AddFn g_add;
ColorKeyFn g_ck;
int g_bmpW = 0, g_bmpH = 0, g_fmt = 13, g_upEvery = 1, g_useCk = 1, g_settle = 0;
void* g_bmp = nullptr;
HDC g_dc;
HBITMAP g_dib;
uint32_t* g_px;
double g_uploadUs = 0;
unsigned long g_uploads = 0;

void CmdBmp(int argc, const char* const* argv) {
    g_bmpW = argc > 1 ? Num(argv[1]) : 0;
    g_bmpH = argc > 2 ? Num(argv[2]) : 0;
    g_fmt = argc > 3 ? Num(argv[3]) : 13;
    g_upEvery = argc > 4 ? Num(argv[4]) : 1;
    g_useCk = argc > 5 ? Num(argv[5]) : 1;
    if (g_upEvery < 1) g_upEvery = 1;
    g_bmp = nullptr;  // leaked on purpose (bench only); a new one is made below
    if (g_uploads) log::Info("bench: bmp upload avg %lu us over %lu uploads",
                             static_cast<unsigned long>(g_uploadUs / g_uploads), g_uploads);
    g_uploadUs = 0, g_uploads = 0;
    log::Info("bench: bmp %dx%d format %d every %d ck %d", g_bmpW, g_bmpH, g_fmt, g_upEvery, g_useCk);
}

void Render(unsigned long n) {  // GDI: dark panel, 26 text lines with a changing counter
    if (!g_dc) g_dc = CreateCompatibleDC(nullptr);
    static int w = 0, h = 0;
    if (w != g_bmpW || h != g_bmpH) {
        if (g_dib) DeleteObject(g_dib);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = g_bmpW, bi.bmiHeader.biHeight = -g_bmpH;
        bi.bmiHeader.biPlanes = 1, bi.bmiHeader.biBitCount = 32, bi.bmiHeader.biCompression = BI_RGB;
        g_dib = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&g_px), nullptr, 0);
        SelectObject(g_dc, g_dib);
        w = g_bmpW, h = g_bmpH;
    }
    RECT all = {0, 0, g_bmpW, g_bmpH};
    SetBkColor(g_dc, RGB(0x10, 0x14, 0x18));
    ExtTextOutA(g_dc, 0, 0, ETO_OPAQUE, &all, "", 0, nullptr);
    SetTextColor(g_dc, RGB(0x7f, 0xd1, 0x7f));
    char t[96];
    for (int i = 0; i < 26 && 4 + i * 13 < g_bmpH; ++i) {
        const int len = wsprintfA(t, "| line %02d  frame %08lu  -123.4  456.7  -89.0  Wall |", i, n);
        TextOutA(g_dc, 4, 4 + i * 13, t, len);
    }
    GdiFlush();
}

bool Upload() {
    void* target = nullptr;
    if (!g_lock(g_bmp, &target, 0, 0, g_fmt) || !target) return false;
    uint32_t info[9] = {};
    g_info(target, info, nullptr);
    const int stride = static_cast<int>(info[2]);
    void* bits = g_bits(target);
    if (bits && stride >= g_bmpW) {
        for (int y = 0; y < g_bmpH; ++y) {
            const uint32_t* s = g_px + y * g_bmpW;
            if (g_fmt == 5) {
                uint16_t* d = static_cast<uint16_t*>(bits) + y * stride;
                for (int x = 0; x < g_bmpW; ++x) {
                    const uint32_t c = s[x];
                    d[x] = static_cast<uint16_t>(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
                }
            } else {
                uint32_t* d = static_cast<uint32_t*>(bits) + y * stride;
                for (int x = 0; x < g_bmpW; ++x) d[x] = s[x] | 0xFF000000u;
            }
        }
    }
    g_unlock(target);
    return bits != nullptr;
}

void Bmp() {
    if (!g_bmpW || !g_create) return;
    void* engine = hooks::CurrentEngine();
    if (!engine) return;
    LARGE_INTEGER t0, t1, fq;
    if (!g_bmp) {
        g_bmp = g_create(g_bmpW, g_bmpH, 1, g_fmt);
        if (!g_bmp) return log::Warn("bench: bmp create failed");
        if (g_useCk) g_ck(g_bmp, 1, g_fmt == 5 ? 0xF81F : 0x00FF00FF, 0);
        Render(g_frame);
        Upload();
        if (!g_add(engine, g_bmp)) return log::Warn("bench: bmp add failed");
        g_settle = 2;
        return;
    }
    if (g_settle > 0) return void(--g_settle);
    if (g_frame % g_upEvery == 0) {
        QueryPerformanceCounter(&t0);
        Render(g_frame);
        Upload();
        QueryPerformanceCounter(&t1);
        QueryPerformanceFrequency(&fq);
        g_uploadUs += static_cast<double>(t1.QuadPart - t0.QuadPart) * 1e6 / static_cast<double>(fq.QuadPart);
        ++g_uploads;
    }
    if (g_flush) g_flush(engine);
    const GeRect r = {0, g_bmpW - 1, 0, g_bmpH - 1};
    g_draw(engine, g_bmp, &r, 230, 12);
}

} // namespace

bool Init() {
    if (!addr::Usable(addr::Id::genesis_printf)) return false;
    g_printf = reinterpret_cast<PrintfFn>(addr::ExportAddress(addr::Id::genesis_printf));
    cmd::RegisterCommand("fd_bench_text", "Debug: fd_bench_text <calls> <chars> <every> <mode>", Cmd);
    hooks::OnFrameEnd(Draw);
    if (addr::Usable(addr::Id::genesis_draw_bitmap)) {
        g_draw = reinterpret_cast<DrawFn>(addr::ExportAddress(addr::Id::genesis_draw_bitmap));
        if (addr::Usable(addr::Id::genesis_flush_pipeline))
            g_flush = reinterpret_cast<FlushFn>(addr::ExportAddress(addr::Id::genesis_flush_pipeline));
        cmd::RegisterCommand("fd_bench_blit", "Debug: fd_bench_blit <n> <w> <h> (font bitmap rect blits)", CmdBlit);
        hooks::OnFrameEnd(Blit);
        using addr::Id;
        g_create = reinterpret_cast<CreateFn>(addr::ExportAddress(Id::genesis_bitmap_create));
        g_lock = reinterpret_cast<LockFn>(addr::ExportAddress(Id::genesis_bitmap_lock_write_format));
        g_unlock = reinterpret_cast<BmpFn>(addr::ExportAddress(Id::genesis_bitmap_unlock));
        g_bits = reinterpret_cast<GetBitsFn>(addr::ExportAddress(Id::genesis_bitmap_get_bits));
        g_info = reinterpret_cast<GetInfoFn>(addr::ExportAddress(Id::genesis_bitmap_get_info));
        g_add = reinterpret_cast<AddFn>(addr::ExportAddress(Id::genesis_add_bitmap));
        g_ck = reinterpret_cast<ColorKeyFn>(addr::ExportAddress(Id::genesis_bitmap_set_color_key));
        cmd::RegisterCommand("fd_bench_bmp", "Debug: fd_bench_bmp <w> <h> <format> <every> <ck>", CmdBmp);
        hooks::OnFrameEnd(Bmp);
    }
    return true;
}

} // namespace fdcore::bench

FDCORE_MODULE(bench, "bench", nullptr, 0, nullptr, &fdcore::bench::Init, 90)

#endif
