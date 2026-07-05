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
        DecorationId::kNone, false, ScopeStyle::kNone,
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
        DecorationId::kSweep, true, ScopeStyle::kNone,
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
        DecorationId::kNone, false, ScopeStyle::kNone,
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
        DecorationId::kNone, false, ScopeStyle::kNone,
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
        DecorationId::kStarfield, false, ScopeStyle::kNone,
    },
    // 5 — Silent Running (submarine night-vision red; brightness ramp + sweep)
    // Values are LOGICAL RGB (themeColor() applies the uniform R/B swap).
    // Replaces The Meatball at index 5: users on the old NVS index 5 wake up
    // in Silent Running (a re-skin, no NVS migration needed).
    {
        "Silent Running",
        {0x0A, 0x00, 0x02},   // bg near-black red
        {0x7A, 0x14, 0x08},   // rings
        {0xFF, 0x5A, 0x3A},   // label
        {0xFF, 0x5A, 0x3A},   // center
        {0xFF, 0x5A, 0x3A},   // tag_type
        {0xFF, 0x5A, 0x3A},   // tag_alt
        {0x5A, 0x0F, 0x06},   // track (crosshairs, dim)
        {0x7A, 0x14, 0x08},   // runway
        {0xFF, 0x5A, 0x3A},   // runway_label
        {0xFF, 0x3A, 0x1E},   // decoration (sweep wedge, base red)
        RampMode::kBrightness,
        {0xFF, 0x3A, 0x1E}, {0xFF, 0x3A, 0x1E}, {0xFF, 0x3A, 0x1E},
        DecorationId::kNone, true, ScopeStyle::kNone,
    },
    // 6 — CIC (combat-information-center vector scope; brightness ramp + sweep)
    // Values are LOGICAL RGB (themeColor() applies the uniform R/B swap).
    // ScopeStyle::kCic drives the bearing ring, minor ticks, square grid,
    // bracket targets, and the CIC-only region map (region_map.cpp).
    {
        "CIC",
        {0x02, 0x06, 0x04},   // bg near-black green
        {0x2A, 0xAB, 0x5A},   // rings
        {0x5A, 0xFF, 0x8A},   // label (bearing-degree labels)
        {0x5A, 0xFF, 0x8A},   // center
        {0x5A, 0xFF, 0x8A},   // tag_type
        {0x5A, 0xFF, 0x8A},   // tag_alt
        {0x2A, 0xAB, 0x5A},   // track
        {0x2A, 0xAB, 0x5A},   // runway
        {0x5A, 0xFF, 0x8A},   // runway_label
        {0x2A, 0xAB, 0x5A},   // decoration (sweep wedge)
        RampMode::kBrightness,
        {0x5A, 0xFF, 0x8A}, {0x5A, 0xFF, 0x8A}, {0x5A, 0xFF, 0x8A},  // targets
        DecorationId::kNone, true, ScopeStyle::kCic,
    },
};
// clang-format on

}  // namespace ui::radar
