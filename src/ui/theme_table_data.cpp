#include "ui/theme_table.h"

namespace ui::radar {

// clang-format off
const Theme kThemes[kThemeCount] = {
    // 0 — Midnight (stock look; ramp red->amber->cyan)
    // All theme values are logical RGB (what you want to see). Midnight is
    // special: it encodes the *rendered* stock appearance (the old firmware
    // packed these constants unswapped on the R/B-transposed panel), so
    // post-swap panel words are byte-identical to stock. The ramp stops are
    // new-feature colors authored in logical RGB like the other themes.
    // Details: docs spec + Task 5 review.
    {
        "Midnight",
        {28, 10, 4},      // bg  (stock kBgR/G/B {4,10,28}, R/B pre-swapped)
        {32, 100, 16},    // grid (stock {16,100,32}, R/B pre-swapped)
        {255, 255, 255},  // label
        {255, 255, 255},  // center
        {0, 200, 255},    // tag_type (stock {255,200,0}, R/B pre-swapped)
        {255, 200, 90},   // tag_alt (stock {90,200,255}, R/B pre-swapped)
        {255, 0, 255},    // track
        {170, 150, 56},   // runway (stock {56,150,170}, R/B pre-swapped)
        {230, 210, 110},  // runway_label (stock {110,210,230}, R/B pre-swapped)
        {32, 100, 16},    // decoration (unused; mirrors grid)
        RampMode::kColor,
        {0xFF, 0x4A, 0x2A}, {0xFF, 0xD2, 0x4A}, {0x39, 0xD0, 0xFF},
        DecorationId::kNone, false,
    },
    // 1 — Phosphor (green CRT + sweep; brightness ramp)
    {
        "Phosphor",
        {0x02, 0x0A, 0x04},
        {0x1E, 0x8A, 0x3C},
        {0x39, 0xD0, 0x6A},
        {0x39, 0xD0, 0x6A},
        {0x2E, 0xC0, 0x5A},
        {0x39, 0xD0, 0x6A},
        {0x1E, 0x8A, 0x3C},
        {0x1E, 0x8A, 0x3C},
        {0x39, 0xD0, 0x6A},
        {0x39, 0xD0, 0x6A},  // sweep wedge color
        RampMode::kBrightness,
        {0x39, 0xD0, 0x6A}, {0x39, 0xD0, 0x6A}, {0x39, 0xD0, 0x6A},
        DecorationId::kSweep, true,
    },
    // 2 — Amber CRT (brightness ramp)
    {
        "Amber CRT",
        {0x0C, 0x05, 0x00},
        {0x8A, 0x5A, 0x10},
        {0xFF, 0xC0, 0x40},
        {0xFF, 0xC0, 0x40},
        {0xF0, 0xB0, 0x30},
        {0xFF, 0xC0, 0x40},
        {0x8A, 0x5A, 0x10},
        {0x8A, 0x5A, 0x10},
        {0xFF, 0xC0, 0x40},
        {0xFF, 0xC0, 0x40},
        RampMode::kBrightness,
        {0xFF, 0xC0, 0x40}, {0xFF, 0xC0, 0x40}, {0xFF, 0xC0, 0x40},
        DecorationId::kNone, false,
    },
    // 3 — Vice (neon pink/violet; ramp pink->amber->cyan)
    {
        "Vice",
        {0x12, 0x04, 0x1F},
        {0xFF, 0x2A, 0x9D},   // rings
        {0xF0, 0xE0, 0xFF},
        {0xFF, 0xFF, 0xFF},
        {0x2A, 0xF5, 0xFF},
        {0xFF, 0xD2, 0x4A},
        {0x7A, 0x2A, 0xFF},   // crosshairs
        {0xFF, 0x2A, 0x9D},
        {0xFF, 0x8A, 0xD0},
        {0x7A, 0x2A, 0xFF},
        RampMode::kColor,
        {0xFF, 0x3A, 0x5A}, {0xFF, 0xD2, 0x4A}, {0x2A, 0xF5, 0xFF},
        DecorationId::kNone, false,
    },
    // 4 — Mission Control (navy/gold + starfield; ramp red->white->gold)
    {
        "Mission Control",
        {0x08, 0x12, 0x28},
        {0xD4, 0xA5, 0x44},   // gold rings
        {0xE8, 0xE2, 0xCE},
        {0xE8, 0xE2, 0xCE},
        {0xD4, 0xA5, 0x44},
        {0xB8, 0xC8, 0xE8},
        {0xD4, 0xA5, 0x44},
        {0x6A, 0x84, 0xB0},
        {0x9A, 0xB4, 0xD8},
        {0xE8, 0xE2, 0xCE},   // star tint
        RampMode::kColor,
        {0xC8, 0x33, 0x2A}, {0xE8, 0xE2, 0xCE}, {0xD4, 0xA5, 0x44},
        DecorationId::kStarfield, false,
    },
    // 5 — The Meatball (NASA blue/white rings + red swoosh; ramp gold->white->orange)
    {
        "The Meatball",
        {0x0B, 0x1E, 0x5B},
        {0xE8, 0xE2, 0xCE},   // white rings
        {0xFF, 0xFF, 0xFF},
        {0xFF, 0xFF, 0xFF},
        {0xE8, 0xE2, 0xCE},
        {0xB8, 0xC8, 0xE8},
        {0xC8, 0x33, 0x2A},
        {0xB8, 0xC8, 0xE8},
        {0xE8, 0xE2, 0xCE},
        {0xC8, 0x33, 0x2A},   // red swoosh
        RampMode::kColor,
        {0xFF, 0xD2, 0x4A}, {0xE8, 0xE2, 0xCE}, {0xFF, 0x8C, 0x5A},
        DecorationId::kMeatball, false,
    },
};
// clang-format on

}  // namespace ui::radar
