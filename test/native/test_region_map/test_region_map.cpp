#include <unity.h>

#include "ui/region_map_geom.h"

using ui::radar::clipSegmentToDisc;
using ui::radar::mapCoversLocation;

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

void test_segment_through_chord_both_endpoints_outside() {
  // The most common runtime case: an 80 km map polyline crossing the whole
  // <= 66 km view. Both endpoints move onto the boundary (t0 and t1 interior).
  float x0 = -100, y0 = 120, x1 = 340, y1 = 120;  // straight through center
  const bool hit = clipSegmentToDisc(120, 120, 107, &x0, &y0, &x1, &y1);
  TEST_ASSERT_TRUE(hit);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 13.0f, x0);   // enters left edge
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 227.0f, x1);  // exits right edge
}

void test_map_covers_same_location() {
  TEST_ASSERT_TRUE(
      mapCoversLocation(36.0999f, -80.2442f, 36.0999f, -80.2442f, 100.0f));
}

void test_map_covers_nearby_location() {
  // ~0.45 deg north is ~50 km — well within the 100 km drift budget.
  TEST_ASSERT_TRUE(mapCoversLocation(36.0999f, -80.2442f, 36.0999f + 0.45f,
                                     -80.2442f, 100.0f));
}

void test_map_does_not_cover_far_location() {
  // Amsterdam vs. the baked Winston-Salem center: thousands of km away.
  TEST_ASSERT_FALSE(
      mapCoversLocation(36.0999f, -80.2442f, 52.37f, 4.90f, 100.0f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_segment_fully_inside_unchanged);
  RUN_TEST(test_segment_fully_outside_misses);
  RUN_TEST(test_segment_crossing_is_clipped_to_boundary);
  RUN_TEST(test_degenerate_point_inside);
  RUN_TEST(test_degenerate_point_outside);
  RUN_TEST(test_segment_through_chord_both_endpoints_outside);
  RUN_TEST(test_map_covers_same_location);
  RUN_TEST(test_map_covers_nearby_location);
  RUN_TEST(test_map_does_not_cover_far_location);
  return UNITY_END();
}
