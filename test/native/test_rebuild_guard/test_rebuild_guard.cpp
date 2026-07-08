#include <unity.h>

#include "services/rebuild_guard.h"

using services::map::RebuildLock;

namespace {
bool s_busy = false;
}  // namespace

void setUp() { s_busy = false; }
void tearDown() {}

void test_first_lock_acquires_and_sets_flag() {
  RebuildLock lock(s_busy);
  TEST_ASSERT_TRUE(lock.acquired());
  TEST_ASSERT_TRUE(s_busy);
}

void test_nested_lock_fails_while_first_held() {
  RebuildLock outer(s_busy);
  TEST_ASSERT_TRUE(outer.acquired());

  RebuildLock inner(s_busy);
  TEST_ASSERT_FALSE(inner.acquired());
  // Flag must still read busy -- the failed nested lock didn't clobber it.
  TEST_ASSERT_TRUE(s_busy);
}

void test_failed_nested_lock_destruction_does_not_clear_flag() {
  RebuildLock outer(s_busy);
  TEST_ASSERT_TRUE(outer.acquired());
  {
    // This nested lock fails to acquire and is destroyed at the end of this
    // scope. Its destruction must be a no-op on the flag -- this is exactly
    // the regression the RAII design (acquired_ gating the destructor)
    // exists to prevent.
    RebuildLock inner(s_busy);
    TEST_ASSERT_FALSE(inner.acquired());
  }
  TEST_ASSERT_TRUE(s_busy);  // still held by outer after inner's dtor ran
}

void test_outer_lock_destruction_clears_flag_and_allows_fresh_acquire() {
  {
    RebuildLock outer(s_busy);
    TEST_ASSERT_TRUE(outer.acquired());
    TEST_ASSERT_TRUE(s_busy);
  }
  TEST_ASSERT_FALSE(s_busy);  // outer's destructor released it

  RebuildLock fresh(s_busy);
  TEST_ASSERT_TRUE(fresh.acquired());
  TEST_ASSERT_TRUE(s_busy);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_lock_acquires_and_sets_flag);
  RUN_TEST(test_nested_lock_fails_while_first_held);
  RUN_TEST(test_failed_nested_lock_destruction_does_not_clear_flag);
  RUN_TEST(test_outer_lock_destruction_clears_flag_and_allows_fresh_acquire);
  return UNITY_END();
}
