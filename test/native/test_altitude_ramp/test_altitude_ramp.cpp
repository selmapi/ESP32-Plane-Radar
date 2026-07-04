#include <unity.h>

#include <cstdint>

#include "ui/altitude_ramp.h"

using ui::radar::Rgb8;
using ui::radar::rampColor;
using ui::radar::rampBrightness;

void test_low_altitude_is_first_stop() {
  const Rgb8 lo{0xFF, 0x4A, 0x2A};
  const Rgb8 mid{0xFF, 0xD2, 0x4A};
  const Rgb8 hi{0x39, 0xD0, 0xFF};
  Rgb8 c = rampColor(0, lo, mid, hi);
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.r);
  TEST_ASSERT_EQUAL_UINT8(0x4A, c.g);
  TEST_ASSERT_EQUAL_UINT8(0x2A, c.b);
}

void test_high_altitude_clamps_to_last_stop() {
  const Rgb8 lo{0xFF, 0x4A, 0x2A};
  const Rgb8 mid{0xFF, 0xD2, 0x4A};
  const Rgb8 hi{0x39, 0xD0, 0xFF};
  Rgb8 c = rampColor(60000, lo, mid, hi);  // above kRampTopFt
  TEST_ASSERT_EQUAL_UINT8(0x39, c.r);
  TEST_ASSERT_EQUAL_UINT8(0xD0, c.g);
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.b);
}

void test_mid_altitude_hits_middle_stop() {
  const Rgb8 lo{0, 0, 0};
  const Rgb8 mid{100, 100, 100};
  const Rgb8 hi{200, 200, 200};
  // kRampMidFt is the altitude that maps exactly to the mid stop.
  Rgb8 c = rampColor(ui::radar::kRampMidFt, lo, mid, hi);
  TEST_ASSERT_UINT8_WITHIN(2, 100, c.r);
  TEST_ASSERT_UINT8_WITHIN(2, 100, c.g);
  TEST_ASSERT_UINT8_WITHIN(2, 100, c.b);
}

void test_unknown_altitude_uses_low_stop() {
  const Rgb8 lo{0xFF, 0x4A, 0x2A};
  const Rgb8 mid{0xFF, 0xD2, 0x4A};
  const Rgb8 hi{0x39, 0xD0, 0xFF};
  Rgb8 c = rampColor(INT32_MIN, lo, mid, hi);
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.r);
}

void test_brightness_scales_base_color() {
  const Rgb8 base{0x1E, 0x8A, 0x3C};
  Rgb8 lo = rampBrightness(0, base);
  Rgb8 hi = rampBrightness(60000, base);
  // Higher altitude -> brighter (each channel scaled up), clamped at 255.
  TEST_ASSERT_TRUE(hi.g >= lo.g);
  TEST_ASSERT_TRUE(lo.g > 0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_low_altitude_is_first_stop);
  RUN_TEST(test_high_altitude_clamps_to_last_stop);
  RUN_TEST(test_mid_altitude_hits_middle_stop);
  RUN_TEST(test_unknown_altitude_uses_low_stop);
  RUN_TEST(test_brightness_scales_base_color);
  return UNITY_END();
}
