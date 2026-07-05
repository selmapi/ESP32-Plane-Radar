#include <unity.h>

#include <cstring>

#include "ui/theme_table.h"

using namespace ui::radar;

void test_seven_themes_present() {
  TEST_ASSERT_EQUAL_size_t(7, kThemeCount);
}

void test_names_nonempty_and_ordered() {
  const char* expect[] = {"Midnight",        "Phosphor", "Amber CRT",
                          "Vice",            "Mission Control",
                          "Silent Running", "CIC"};
  for (size_t i = 0; i < kThemeCount; ++i) {
    TEST_ASSERT_NOT_NULL(kThemes[i].name);
    TEST_ASSERT_TRUE(kThemes[i].name[0] != '\0');
    TEST_ASSERT_EQUAL_STRING(expect[i], kThemes[i].name);
  }
}

void test_sweep_enabled_set() {
  // Phosphor (1), Silent Running (5) sweep; CIC (6) does not (product-owner
  // restyle dropped the sweep in favor of a static natural-color map).
  const bool expect[] = {false, true, false, false, false, true, false};
  for (size_t i = 0; i < kThemeCount; ++i) {
    TEST_ASSERT_EQUAL(expect[i], kThemes[i].sweep_enabled);
  }
}

void test_scope_style_only_on_cic() {
  for (size_t i = 0; i < kThemeCount; ++i) {
    const ScopeStyle expect = (i == 6) ? ScopeStyle::kCic : ScopeStyle::kNone;
    TEST_ASSERT_EQUAL(expect, kThemes[i].scope_style);
  }
}

void test_decoration_ids_valid() {
  for (size_t i = 0; i < kThemeCount; ++i) {
    const DecorationId d = kThemes[i].decoration_id;
    TEST_ASSERT_TRUE(d == DecorationId::kNone || d == DecorationId::kSweep ||
                     d == DecorationId::kStarfield ||
                     d == DecorationId::kMeatball);
  }
}

void test_mono_themes_use_brightness_ramp() {
  TEST_ASSERT_EQUAL(RampMode::kBrightness, kThemes[1].ramp_mode);  // Phosphor
  TEST_ASSERT_EQUAL(RampMode::kBrightness, kThemes[2].ramp_mode);  // Amber
  TEST_ASSERT_EQUAL(RampMode::kBrightness, kThemes[5].ramp_mode);  // Silent
  TEST_ASSERT_EQUAL(RampMode::kBrightness, kThemes[6].ramp_mode);  // CIC
  TEST_ASSERT_EQUAL(RampMode::kColor, kThemes[0].ramp_mode);       // Midnight
}

void test_midnight_colors_anchor_layout() {
  // Midnight stores R/B pre-swapped stock constants (see theme_table_data.cpp):
  // bg {4,10,28} -> {28,10,4}, grid {16,100,32} -> {32,100,16}. Ramp stops are
  // logical RGB, unswapped.
  TEST_ASSERT_EQUAL_UINT8(28, kThemes[0].bg.r);
  TEST_ASSERT_EQUAL_UINT8(10, kThemes[0].bg.g);
  TEST_ASSERT_EQUAL_UINT8(4, kThemes[0].bg.b);
  TEST_ASSERT_EQUAL_UINT8(32, kThemes[0].grid.r);
  TEST_ASSERT_EQUAL_UINT8(100, kThemes[0].grid.g);
  TEST_ASSERT_EQUAL_UINT8(16, kThemes[0].grid.b);
  TEST_ASSERT_EQUAL_UINT8(0xFF, kThemes[0].ramp_low.r);
  TEST_ASSERT_EQUAL_UINT8(0x39, kThemes[0].ramp_high.r);
}

void test_vice_colors_anchor_layout() {
  TEST_ASSERT_EQUAL_UINT8(0xFF, kThemes[3].grid.r);
  TEST_ASSERT_EQUAL_UINT8(0x2A, kThemes[3].grid.g);
  TEST_ASSERT_EQUAL_UINT8(0x9D, kThemes[3].grid.b);
  TEST_ASSERT_EQUAL_UINT8(0x7A, kThemes[3].track.r);
  TEST_ASSERT_EQUAL_UINT8(0x2A, kThemes[3].track.g);
  TEST_ASSERT_EQUAL_UINT8(0xFF, kThemes[3].track.b);
}

void test_silent_running_colors_anchor_layout() {
  // Logical RGB (unswapped in the table): near-black red bg, orange-red labels.
  TEST_ASSERT_EQUAL_UINT8(0x0A, kThemes[5].bg.r);
  TEST_ASSERT_EQUAL_UINT8(0x00, kThemes[5].bg.g);
  TEST_ASSERT_EQUAL_UINT8(0x02, kThemes[5].bg.b);
  TEST_ASSERT_EQUAL_UINT8(0xFF, kThemes[5].label.r);
  TEST_ASSERT_EQUAL_UINT8(0x5A, kThemes[5].label.g);
  TEST_ASSERT_EQUAL_UINT8(0x3A, kThemes[5].label.b);
}

void test_cic_colors_anchor_layout() {
  TEST_ASSERT_EQUAL_UINT8(0x00, kThemes[6].bg.r);
  TEST_ASSERT_EQUAL_UINT8(0x00, kThemes[6].bg.g);
  TEST_ASSERT_EQUAL_UINT8(0x00, kThemes[6].bg.b);
  TEST_ASSERT_EQUAL_UINT8(0x2A, kThemes[6].grid.r);
  TEST_ASSERT_EQUAL_UINT8(0xAB, kThemes[6].grid.g);
  TEST_ASSERT_EQUAL_UINT8(0x5A, kThemes[6].grid.b);
  TEST_ASSERT_EQUAL_UINT8(0x5A, kThemes[6].tag_type.r);
  TEST_ASSERT_EQUAL_UINT8(0xFF, kThemes[6].tag_type.g);
  TEST_ASSERT_EQUAL_UINT8(0x8A, kThemes[6].tag_type.b);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_seven_themes_present);
  RUN_TEST(test_names_nonempty_and_ordered);
  RUN_TEST(test_sweep_enabled_set);
  RUN_TEST(test_scope_style_only_on_cic);
  RUN_TEST(test_decoration_ids_valid);
  RUN_TEST(test_mono_themes_use_brightness_ramp);
  RUN_TEST(test_midnight_colors_anchor_layout);
  RUN_TEST(test_vice_colors_anchor_layout);
  RUN_TEST(test_silent_running_colors_anchor_layout);
  RUN_TEST(test_cic_colors_anchor_layout);
  return UNITY_END();
}
