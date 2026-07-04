#include <unity.h>

#include "ui/selection_logic.h"

using ui::radar::selectionShouldClear;

void test_no_selection_never_clears() {
  TEST_ASSERT_FALSE(selectionShouldClear(false, /*present=*/false,
                                         /*last_poll_ms=*/0, /*now_ms=*/999999,
                                         /*timeout_ms=*/30000));
}

void test_clears_when_plane_absent() {
  TEST_ASSERT_TRUE(selectionShouldClear(true, /*present=*/false,
                                        /*last_poll_ms=*/1000, /*now_ms=*/1500,
                                        /*timeout_ms=*/30000));
}

void test_clears_after_poll_timeout() {
  TEST_ASSERT_TRUE(selectionShouldClear(true, /*present=*/true,
                                        /*last_poll_ms=*/1000,
                                        /*now_ms=*/1000 + 30001,
                                        /*timeout_ms=*/30000));
}

void test_holds_when_present_and_recent_poll() {
  TEST_ASSERT_FALSE(selectionShouldClear(true, /*present=*/true,
                                         /*last_poll_ms=*/1000,
                                         /*now_ms=*/1000 + 5000,
                                         /*timeout_ms=*/30000));
}

void test_wraparound_recent_poll_holds() {
  // last poll just before millis() wrap; now just after: delta is small.
  TEST_ASSERT_FALSE(selectionShouldClear(true, /*present=*/true,
                                         /*last_poll_ms=*/4294967295UL - 100UL,
                                         /*now_ms=*/200UL,
                                         /*timeout_ms=*/30000UL));
}

void test_wraparound_expired_clears() {
  TEST_ASSERT_TRUE(selectionShouldClear(true, /*present=*/true,
                                        /*last_poll_ms=*/4294967295UL - 100UL,
                                        /*now_ms=*/40000UL,
                                        /*timeout_ms=*/30000UL));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_selection_never_clears);
  RUN_TEST(test_clears_when_plane_absent);
  RUN_TEST(test_clears_after_poll_timeout);
  RUN_TEST(test_holds_when_present_and_recent_poll);
  RUN_TEST(test_wraparound_recent_poll_holds);
  RUN_TEST(test_wraparound_expired_clears);
  return UNITY_END();
}
