// gfx: reusable in-frame 2D drawing blocks (owner request after the alpha spike,
// core/evidence/alpha_spike/README.md). Everything runs on the game thread at
// frame end (hooks::OnFrameEnd); textdraw is built on these.
//
//   Canvas        GDI text on a monospace grid -> 32-bit pixels (0x00RRGGBB).
//   Decal         opaque 565 engine bitmap, geEngine_DrawBitmap. Queued: drawn at
//                 EndFrame's flush, on top of everything. Works without a world (menus).
//   AlphaSurface  per-pixel alpha: <=256 px 4444 tiles attached to the rendered
//                 world (geWorld_AddBitmap), drawn with geEngine_RenderPoly. Needs a
//                 world this frame; only changed tiles are re-uploaded.
//   SolidRect     untextured opaque quad (D3DDrv ignores vertex alpha on these).
//
// Order within a frame: BeginDraw() once (flushes the cached 3D, or immediate polys
// would be drawn under it), then AlphaSurface / SolidRect draw immediately; Decals
// queue and land on top at EndFrame.
#pragma once

#include <stdint.h>

namespace fdcore::gfx {

bool Init();   // resolves the Genesis API, hooks geEngine_RenderWorld; false = no in-frame drawing
bool Ready();

void* Engine();  // geEngine* of the frame in progress
void* World();   // geWorld* rendered this frame (null when none was)
// Flushes the cached 3D once per frame; call before the first immediate draw.
// False when there is no engine this frame.
bool BeginDraw();
// The render view size (the camera rect; the window as fallback).
void ViewSize(int* w, int* h);

// --- Canvas ------------------------------------------------------------------------
// One shared font (Consolas, NONANTIALIASED, `fd_text_size` px). Text pixels are
// exactly the colour given, so surfaces can key alpha on the background colour.
class Canvas {
public:
    // Sizes the canvas for cols x rows cells plus `pad` px all round, filled with bg.
    bool Begin(int cols, int rows, int pad, uint32_t bg);
    // One run of text at a cell position; bold selects the bold font.
    void Text(int col, int row, const char* s, int len, uint32_t fg, uint32_t bg, bool bold);
    void Finish();  // GdiFlush before the pixels are read

    const uint32_t* Pixels() const { return px_; }
    int Stride() const { return cap_w_; }  // pixels per row
    int Width() const { return w_; }       // content size
    int Height() const { return h_; }

private:
    void* dib_ = nullptr;
    uint32_t* px_ = nullptr;
    int w_ = 0, h_ = 0, cap_w_ = 0, cap_h_ = 0, pad_ = 0;
};
// Cell size of the current font; bumps FontGeneration() when fd_text_size changes.
void CellSize(int* w, int* h);
unsigned FontGeneration();

// --- Decal -------------------------------------------------------------------------
class Decal {
public:
    bool Upload(const uint32_t* px, int stride, int w, int h);  // (re)creates as needed
    bool Draw(int x, int y);  // false while settling / without a bitmap
    void Destroy();
    bool Exists() const { return bmp_ != nullptr; }

private:
    void* bmp_ = nullptr;
    void* engine_ = nullptr;
    int cap_w_ = 0, cap_h_ = 0, w_ = 0, h_ = 0, settle_ = 0;
};

// --- AlphaSurface --------------------------------------------------------------------
// Alpha by background colour: pixels equal to a key's rgb get its alpha, all others 255.
struct AlphaKey {
    uint32_t rgb;
    uint8_t alpha;
};
class AlphaSurface {
public:
    static constexpr int kTile = 256;  // D3DDrv's 3D texture limit (512 crashes it)
    static constexpr int kMaxTiles = 9;  // up to 768 x 768

    bool Upload(const uint32_t* px, int stride, int w, int h, const AlphaKey* keys, int nKeys);
    // False when it can't draw this frame (no world, or tiles still settling); the
    // caller may fall back to a Decal.
    bool Draw(int x, int y);
    void Destroy();
    int TilesUploaded() const { return uploads_; }

private:
    struct Tile {
        void* bmp;
        uint16_t* shadow;  // last uploaded 4444 texels (dirty check)
        void* world;       // the world it is attached to
        int settle;
    };
    Tile tiles_[kMaxTiles] = {};
    int cols_ = 0, rows_ = 0, w_ = 0, h_ = 0, uploads_ = 0;
    bool Attach(Tile& t);
};

// --- SolidRect -------------------------------------------------------------------------
bool SolidRect(int x, int y, int w, int h, uint32_t rgb);

} // namespace fdcore::gfx
