#include "test_framework.h"
#include "threads.h"
#include "sentinel_internal.h"
#include <pthread.h>
#include <unistd.h>

static volatile uint32_t g_child_observed_tid = 0;
static vector_clock_t g_child_start_hb;
static vector_clock_t g_child_start_st;

static void *dummy_thread_func(void *arg) {
    (void)arg;
    g_child_observed_tid = ts_get_tid();
    
    ts_lock_runtime();
    ts_thread_info_t *info = ts_current_thread_info();
    vc_copy(&g_child_start_hb, &info->hb_clock);
    vc_copy(&g_child_start_st, &info->st_clock);
    ts_unlock_runtime();
    
    return NULL;
}

TEST_CASE(test_main_thread_init) {
    uint32_t tid = ts_get_tid();
    ASSERT_EQ(tid, 0);
    
    ts_thread_info_t *info = ts_get_thread_info(0);
    ASSERT_TRUE(info != NULL);
    ASSERT_TRUE(info->active);
    ASSERT_EQ(info->hb_clock.clock[0], 1);
    ASSERT_EQ(info->st_clock.clock[0], 1);
}

TEST_CASE(test_create_join_clock_propagation) {
    ts_thread_info_t *main_info = ts_get_thread_info(0);
    uint32_t parent_hb_before = main_info->hb_clock.clock[0];
    
    pthread_t th;
    int rc = ts_pthread_create(&th, NULL, dummy_thread_func, NULL);
    ASSERT_EQ(rc, 0);
    
    /* Rule: create(t -> u): C_t[t]++ */
    ASSERT_EQ(main_info->hb_clock.clock[0], parent_hb_before + 1);
    
    rc = ts_pthread_join(th, NULL);
    ASSERT_EQ(rc, 0);
    
    /* Check child tid and initial clocks */
    ASSERT_TRUE(g_child_observed_tid > 0);
    ASSERT_EQ(g_child_start_hb.clock[g_child_observed_tid], 1);
    ASSERT_EQ(g_child_start_st.clock[g_child_observed_tid], 1);
    ASSERT_EQ(g_child_start_hb.clock[0], parent_hb_before);
    
    /* Rule: join(t waits for u): C_t = max(C_t, C_u_final); C_t[t]++ */
    ASSERT_TRUE(main_info->hb_clock.clock[g_child_observed_tid] >= 1);
    ASSERT_TRUE(main_info->st_clock.clock[g_child_observed_tid] >= 1);
}

int main(void) {
    ts_runtime_init();
    printf("=== Running Threads & Clock Propagation Unit Tests ===\n");
    RUN_TEST(test_main_thread_init);
    RUN_TEST(test_create_join_clock_propagation);
    
    TEST_REPORT();
    return g_tests_failed > 0 ? 1 : 0;
}
