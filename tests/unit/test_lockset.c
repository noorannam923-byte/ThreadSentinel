#include "test_framework.h"
#include "lockset.h"

TEST_CASE(test_lock_id_assignment) {
    int dummy1 = 1;
    int dummy2 = 2;
    
    int id1 = lockset_assign_id(&dummy1);
    ASSERT_TRUE(id1 >= 0);
    
    int id1_repeat = lockset_assign_id(&dummy1);
    ASSERT_EQ(id1, id1_repeat);
    
    int id2 = lockset_assign_id(&dummy2);
    ASSERT_TRUE(id2 >= 0);
    ASSERT_TRUE(id1 != id2);
    
    ASSERT_EQ(lockset_get_id(&dummy1), id1);
    ASSERT_EQ(lockset_get_id(&dummy2), id2);
}

TEST_CASE(test_lockset_bitmask_operations) {
    uint64_t held = 0;
    ASSERT_EQ(held, 0);
    
    lockset_add_held(&held, 3);
    ASSERT_EQ(held, (1ULL << 3));
    
    lockset_add_held(&held, 5);
    ASSERT_EQ(held, (1ULL << 3) | (1ULL << 5));
    
    lockset_remove_held(&held, 3);
    ASSERT_EQ(held, (1ULL << 5));
    
    lockset_remove_held(&held, 5);
    ASSERT_EQ(held, 0);
}

TEST_CASE(test_eraser_state_virgin_to_exclusive) {
    eraser_tracker_t tracker;
    lockset_init_tracker(&tracker);
    
    ASSERT_EQ(tracker.state, ERASER_VIRGIN);
    ASSERT_EQ(tracker.candidate_locks, ~0ULL);
    
    /* First access by thread 1 (write or read) enters EXCLUSIVE state */
    lockset_update(&tracker, 1, true, 0);
    ASSERT_EQ(tracker.state, ERASER_EXCLUSIVE);
    ASSERT_EQ(tracker.first_tid, 1);
    ASSERT_FALSE(lockset_has_violation(&tracker));
    
    /* Subsequent access by thread 1 remains EXCLUSIVE without locks */
    lockset_update(&tracker, 1, false, 0);
    ASSERT_EQ(tracker.state, ERASER_EXCLUSIVE);
    ASSERT_FALSE(lockset_has_violation(&tracker));
}

TEST_CASE(test_eraser_exclusive_to_shared_to_modified) {
    eraser_tracker_t tracker;
    lockset_init_tracker(&tracker);
    
    /* T1 initializes */
    lockset_update(&tracker, 1, true, 0);
    ASSERT_EQ(tracker.state, ERASER_EXCLUSIVE);
    
    /* T2 reads: transitions to SHARED */
    uint64_t t2_locks = (1ULL << 2);
    lockset_update(&tracker, 2, false, t2_locks);
    ASSERT_EQ(tracker.state, ERASER_SHARED);
    ASSERT_EQ(tracker.candidate_locks, t2_locks);
    ASSERT_FALSE(lockset_has_violation(&tracker));
    
    /* T3 writes: transitions to SHARED_MODIFIED */
    uint64_t t3_locks = (1ULL << 2); /* Common lock 2 held */
    lockset_update(&tracker, 3, true, t3_locks);
    ASSERT_EQ(tracker.state, ERASER_SHARED_MODIFIED);
    ASSERT_EQ(tracker.candidate_locks, (1ULL << 2));
    ASSERT_FALSE(lockset_has_violation(&tracker));
    
    /* T4 writes with NO locks: candidate set becomes empty -> VIOLATION */
    lockset_update(&tracker, 4, true, 0);
    ASSERT_EQ(tracker.state, ERASER_SHARED_MODIFIED);
    ASSERT_EQ(tracker.candidate_locks, 0);
    ASSERT_TRUE(lockset_has_violation(&tracker));
}

TEST_CASE(test_eraser_exclusive_directly_to_shared_modified) {
    eraser_tracker_t tracker;
    lockset_init_tracker(&tracker);
    
    /* T1 writes in EXCLUSIVE */
    lockset_update(&tracker, 1, true, 0);
    ASSERT_EQ(tracker.state, ERASER_EXCLUSIVE);
    
    /* T2 writes without locks directly moves to SHARED_MODIFIED */
    lockset_update(&tracker, 2, true, 0);
    ASSERT_EQ(tracker.state, ERASER_SHARED_MODIFIED);
    ASSERT_EQ(tracker.candidate_locks, 0);
    ASSERT_TRUE(lockset_has_violation(&tracker));
}

TEST_CASE(test_eraser_read_only_sharing_is_safe) {
    eraser_tracker_t tracker;
    lockset_init_tracker(&tracker);
    
    /* T1 initializes (writes) */
    lockset_update(&tracker, 1, true, 0);
    
    /* Multiple threads read with NO locks: state is SHARED, C(v)=0, but NO violation */
    lockset_update(&tracker, 2, false, 0);
    lockset_update(&tracker, 3, false, 0);
    ASSERT_EQ(tracker.state, ERASER_SHARED);
    ASSERT_EQ(tracker.candidate_locks, 0);
    /* Read-only sharing is safe! */
    ASSERT_FALSE(lockset_has_violation(&tracker));
}

int main(void) {
    printf("=== Running Lockset & Eraser State Machine Unit Tests ===\n");
    RUN_TEST(test_lock_id_assignment);
    RUN_TEST(test_lockset_bitmask_operations);
    RUN_TEST(test_eraser_state_virgin_to_exclusive);
    RUN_TEST(test_eraser_exclusive_to_shared_to_modified);
    RUN_TEST(test_eraser_exclusive_directly_to_shared_modified);
    RUN_TEST(test_eraser_read_only_sharing_is_safe);
    
    TEST_REPORT();
    return g_tests_failed > 0 ? 1 : 0;
}
