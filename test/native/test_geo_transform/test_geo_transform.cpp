#include <unity.h>

#include <cmath>

#include "ui/geo_transform.h"

using ui::radar::offsetKmDelta;

void test_one_deg_east_at_36N_is_shrunk() {
  float dx = 0.0f;
  float dy = 0.0f;
  offsetKmDelta(36.0f, -80.0f + 1.0f, 36.0f, -80.0f, &dx, &dy);
  // cos(36 deg) = 0.80902; 111 * 0.80902 = 89.80 km.
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 89.80f, dx);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, dy);
}

void test_one_deg_north_is_unscaled() {
  float dx = 0.0f;
  float dy = 0.0f;
  offsetKmDelta(37.0f, -80.0f, 36.0f, -80.0f, &dx, &dy);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 111.0f, dy);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, dx);
}

void test_at_equator_longitude_is_unscaled() {
  float dx = 0.0f;
  float dy = 0.0f;
  offsetKmDelta(0.0f, 1.0f, 0.0f, 0.0f, &dx, &dy);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 111.0f, dx);
}

void test_west_offset_is_negative() {
  float dx = 0.0f;
  float dy = 0.0f;
  offsetKmDelta(36.0f, -81.0f, 36.0f, -80.0f, &dx, &dy);
  TEST_ASSERT_TRUE(dx < 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -89.80f, dx);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_one_deg_east_at_36N_is_shrunk);
  RUN_TEST(test_one_deg_north_is_unscaled);
  RUN_TEST(test_at_equator_longitude_is_unscaled);
  RUN_TEST(test_west_offset_is_negative);
  return UNITY_END();
}
