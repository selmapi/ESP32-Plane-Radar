#include <unity.h>

#include "ui/region_map_geom.h"

using ui::radar::clipSegmentToDisc;

void test_segment_fully_inside_unchanged() {
  float x0 = 110, y0 = 120, x1 = 130, y1 = 120;
  const bool hit = clipSegmentToDisc(120, 120, 107, &x0, &y0, &x1, &y1);
  TEST_ASSERT_TRUE(hit);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 110.0f, x0);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 130.0f, x1);
}

void test_segment_fully_outside_misses() {
  float x0 = 5, y0 = 5, x1 = 20, y1 = 5;  // top-left corner, outside r=107
  const bool hit = clipSegmentToDisc(120, 120, 107, &x0, &y0, &x1, &y1);
  TEST_ASSERT_FALSE(hit);
}

void test_segment_crossing_is_clipped_to_boundary() {
  // Horizontal line through center, from far left to center.
  float x0 = -100, y0 = 120, x1 = 120, y1 = 120;
  const bool hit = clipSegmentToDisc(120, 120, 107, &x0, &y0, &x1, &y1);
  TEST_ASSERT_TRUE(hit);
  // Entry point should sit on the left edge of the disc: x = 120 - 107 = 13.
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 13.0f, x0);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, y0);
}

void test_degenerate_point_inside() {
  float x0 = 120, y0 = 120, x1 = 120, y1 = 120;
  TEST_ASSERT_TRUE(clipSegmentToDisc(120, 120, 107, &x0, &y0, &x1, &y1));
}

void test_degenerate_point_outside() {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  TEST_ASSERT_FALSE(clipSegmentToDisc(120, 120, 107, &x0, &y0, &x1, &y1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_segment_fully_inside_unchanged);
  RUN_TEST(test_segment_fully_outside_misses);
  RUN_TEST(test_segment_crossing_is_clipped_to_boundary);
  RUN_TEST(test_degenerate_point_inside);
  RUN_TEST(test_degenerate_point_outside);
  return UNITY_END();
}
