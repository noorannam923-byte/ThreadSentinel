#include "test_framework.h"
#include "deadlock.h"

/* Lock ids are plain integers here; held masks are built with BIT(). */
#define BIT(i) (1ULL << (i))

TEST_CASE(test_inverted_order_two_threads_is_reported) {
    deadlock_init();
    deadlock_on_acquire(1, 0, 0, "&a", "t.c:1");       /* T1: a */
    deadlock_on_acquire(1, 1, BIT(0), "&b", "t.c:2");  /* T1: b while holding a */
    deadlock_on_acquire(2, 1, 0, "&b", "t.c:3");       /* T2: b */
    deadlock_on_acquire(2, 0, BIT(1), "&a", "t.c:4");  /* T2: a while holding b */
    ASSERT_EQ(deadlock_count(), 1);
}

TEST_CASE(test_consistent_order_is_not_reported) {
    deadlock_init();
    deadlock_on_acquire(1, 0, 0, "&a", NULL);
    deadlock_on_acquire(1, 1, BIT(0), "&b", NULL);
    deadlock_on_acquire(2, 0, 0, "&a", NULL);
    deadlock_on_acquire(2, 1, BIT(0), "&b", NULL);
    ASSERT_EQ(deadlock_count(), 0);
}

TEST_CASE(test_single_thread_inversion_is_not_reported) {
    /* One thread taking a->b and later b->a cannot deadlock with itself */
    deadlock_init();
    deadlock_on_acquire(1, 0, 0, "&a", NULL);
    deadlock_on_acquire(1, 1, BIT(0), "&b", NULL);
    deadlock_on_acquire(1, 0, BIT(1), "&a", NULL);
    ASSERT_EQ(deadlock_count(), 0);
}

TEST_CASE(test_lock_never_acquired_through_runtime_is_ignored) {
    /* Held bits for ids never registered (e.g. prevention guard stripes) add no edges */
    deadlock_init();
    deadlock_on_acquire(1, 1, BIT(5), "&b", NULL);
    deadlock_on_acquire(2, 5, BIT(1), "&s", NULL);
    ASSERT_EQ(deadlock_count(), 0);
}

TEST_CASE(test_second_thread_completes_known_cycle) {
    /* T1 takes both orders (no report), then T2 repeats one of them: now reportable */
    deadlock_init();
    deadlock_on_acquire(1, 0, 0, "&a", NULL);
    deadlock_on_acquire(1, 1, BIT(0), "&b", NULL);
    deadlock_on_acquire(1, 0, BIT(1), "&a", NULL);
    ASSERT_EQ(deadlock_count(), 0);
    deadlock_on_acquire(2, 0, BIT(1), "&a", NULL);
    ASSERT_EQ(deadlock_count(), 1);
}

TEST_CASE(test_common_gate_lock_suppresses_report) {
    /* Both inversions happen while holding g (id 2): they cannot interleave */
    deadlock_init();
    deadlock_on_acquire(1, 2, 0, "&g", NULL);
    deadlock_on_acquire(1, 0, BIT(2), "&a", NULL);
    deadlock_on_acquire(1, 1, BIT(2) | BIT(0), "&b", NULL);
    deadlock_on_acquire(2, 2, 0, "&g", NULL);
    deadlock_on_acquire(2, 1, BIT(2), "&b", NULL);
    deadlock_on_acquire(2, 0, BIT(2) | BIT(1), "&a", NULL);
    ASSERT_EQ(deadlock_count(), 0);
}

TEST_CASE(test_three_lock_cycle_is_reported_once) {
    deadlock_init();
    deadlock_on_acquire(1, 0, 0, "&a", NULL);
    deadlock_on_acquire(1, 1, BIT(0), "&b", NULL);   /* a -> b */
    deadlock_on_acquire(2, 2, BIT(1), "&c", NULL);   /* b -> c */
    deadlock_on_acquire(3, 0, BIT(2), "&a", NULL);   /* c -> a closes the cycle */
    ASSERT_EQ(deadlock_count(), 1);
    deadlock_on_acquire(1, 0, BIT(2), "&a", NULL);   /* same cycle again: no duplicate */
    ASSERT_EQ(deadlock_count(), 1);
}

int main(void) {
    printf("=== Running Deadlock (Lock-Order Graph) Unit Tests ===\n");
    g_ts_config.quiet = 1;
    RUN_TEST(test_inverted_order_two_threads_is_reported);
    RUN_TEST(test_consistent_order_is_not_reported);
    RUN_TEST(test_single_thread_inversion_is_not_reported);
    RUN_TEST(test_second_thread_completes_known_cycle);
    RUN_TEST(test_common_gate_lock_suppresses_report);
    RUN_TEST(test_lock_never_acquired_through_runtime_is_ignored);
    RUN_TEST(test_three_lock_cycle_is_reported_once);
    TEST_REPORT();
    return g_tests_failed == 0 ? 0 : 1;
}
