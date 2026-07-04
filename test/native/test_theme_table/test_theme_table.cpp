#include <unity.h>

#include <cstring>

#include "ui/theme_table.h"

using namespace ui::radar;

void test_six_themes_present() {
  TEST_ASSERT_EQUAL_size_t(6, kThemeCount);
}

void test_names_nonempty_and_ordered() {
  const char* expect[] = {"Midnight", "Phosphor",        "Amber CRT",
                          "Vice",     "Mission Control", "The Meatball"};
  for (size_t i = 0; i < kThemeCount; ++i) {
    TEST_ASSERT_NOT_NULL(kThemes[i].name);
    TEST_ASSERT_TRUE(kThemes[i].name[0] != '\0');
    TEST_ASSERT_EQUAL_STRING(expect[i], kThemes[i].name);
  }
}

void test_sweep_only_on_phosphor() {
  for (size_t i = 0; i < kThemeCount; ++i) {
    const bool expect_sweep = (i == 1);
    TEST_ASSERT_EQUAL(expect_sweep, kThemes[i].sweep_enabled);
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
  TEST_ASSERT_EQUAL(RampMode::kColor, kThemes[0].ramp_mode);       // Midnight
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_six_themes_present);
  RUN_TEST(test_names_nonempty_and_ordered);
  RUN_TEST(test_sweep_only_on_phosphor);
  RUN_TEST(test_decoration_ids_valid);
  RUN_TEST(test_mono_themes_use_brightness_ramp);
  return UNITY_END();
}
