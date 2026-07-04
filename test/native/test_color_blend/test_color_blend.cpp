#include <unity.h>

#include <cstdint>

#include "ui/color_blend.h"

using ui::radar::lerpRgb565;

void test_alpha_255_returns_from() {
  TEST_ASSERT_EQUAL_HEX16(0xF800, lerpRgb565(0xF800, 0x001F, 255));
}

void test_alpha_0_returns_to() {
  TEST_ASSERT_EQUAL_HEX16(0x001F, lerpRgb565(0xF800, 0x001F, 0));
}

void test_black_to_white_midpoint_is_grey() {
  // from=white (0xFFFF), to=black (0x0000), alpha=128 ~= half toward black.
  const uint16_t mid = lerpRgb565(0xFFFF, 0x0000, 128);
  const uint16_t r = (mid >> 11) & 0x1F;
  const uint16_t g = (mid >> 5) & 0x3F;
  const uint16_t b = mid & 0x1F;
  TEST_ASSERT_UINT16_WITHIN(1, 15, r);  // ~half of 31
  TEST_ASSERT_UINT16_WITHIN(1, 32, g);  // ~half of 63
  TEST_ASSERT_UINT16_WITHIN(1, 15, b);
}

void test_same_endpoints_are_stable() {
  TEST_ASSERT_EQUAL_HEX16(0x07E0, lerpRgb565(0x07E0, 0x07E0, 200));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_alpha_255_returns_from);
  RUN_TEST(test_alpha_0_returns_to);
  RUN_TEST(test_black_to_white_midpoint_is_grey);
  RUN_TEST(test_same_endpoints_are_stable);
  return UNITY_END();
}
