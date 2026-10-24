// The collision overlay's colours (tools/collision_overlay COLORS / SOURCE_COLORS /
// PLAYER_COLORS), as a textdraw palette: a tag is the colour index, BG_WARP /
// BG_FLASH are textdraw background slots 1 / 2 (both set = slot 3, drawn as flash).
#pragma once

#include <stdint.h>

#include "textdraw.h"

namespace fdcore::glitch::palette {

enum Tag : uint8_t {
    T_FRAME, T_TITLE, T_HEADER, T_TEXT, T_DIM, T_ALERT, T_OK, T_WARN, T_WHITE,
    T_SRC_VICTIM, T_SRC_FIRE, T_SRC_HOSE, T_SRC_HOSEROPE, T_SRC_AXE, T_SRC_PLAYER, T_SRC_ENGINE, T_SRC_OTHER,
    T_PL_STANDING, T_PL_CROUCHED, T_PL_CLIMBING, T_PL_OUTCOLD, T_PL_SCRIPTED, T_PL_BUGGED, T_PL_GROUND, T_PL_INAIR,
    T_COUNT
};
static_assert(T_COUNT <= textdraw::kMaxColors, "tags are textdraw colour indices");
constexpr uint8_t BG_WARP = 1 << textdraw::A_BG_SHIFT, BG_FLASH = 2 << textdraw::A_BG_SHIFT,
                  TAG_MASK = textdraw::A_COLOR_MASK;

constexpr uint32_t kFg[T_COUNT] = {
    0x7c8894, 0xd8e0e8, 0x8a96a2, 0xe8e8e8, 0x5c646c, 0xff3b3b, 0x7fd17f, 0xffcc4d, 0xffffff,
    0xffa040, 0xff5a5a, 0x5aaaff, 0x3f7fbf, 0xffd84a, 0xd0d0d0, 0x8a8a8a, 0xb48cff,
    0x7fd17f, 0x5ad1d1, 0x5aaaff, 0xff5a5a, 0xb48cff, 0xff8c1a, 0x7fd17f, 0xffcc4d,
};
// Panel, warp, flash, warp+flash (= flash).
constexpr uint32_t kBg[4] = {0x101418, 0x5a1010, 0xb02020, 0xb02020};

// Title / alert / warp / flash / Bugged are bold in the original: the textdraw attribute.
inline uint8_t Attr(uint8_t a) {
    const uint8_t t = a & TAG_MASK;
    const bool bold = t == T_TITLE || t == T_ALERT || t == T_PL_BUGGED || (a & (BG_WARP | BG_FLASH));
    return bold ? static_cast<uint8_t>(a | textdraw::A_BOLD) : a;
}

} // namespace fdcore::glitch::palette
