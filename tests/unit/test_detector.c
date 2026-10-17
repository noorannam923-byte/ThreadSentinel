#include "test_framework.h"
#include "detector.h"
#include "sentinel_internal.h"
#include "shadow.h"
#include "threads.h"
#include "report.h"

TEST_CASE(test_detector_modes_configuration) {
    g_ts_config.mode = TS_MODE_DETECT;
    
    g_ts_config.detector = TS_DETECTOR_HYBRID;
    ASSERT_EQ(g_ts_config.detector, TS_DETECTOR_HYBRID);
    
    g_ts_config.detector = TS_DETECTOR_LOCKSET;
    ASSERT_EQ(g_ts_config.detector, TS_DETECTOR_LOCKSET);
    
    g_ts_config.detector = TS_DETECTOR_HB;
    ASSERT_EQ(g_ts_config.detector, TS_DETECTOR_HB);
    
    /* Reset to hybrid */
    g_ts_config.detector = TS_DETECTOR_HYBRID;
}

TEST_CASE(test_detector_concurrent_write_write_race) {
    report_init("test_report.json", 1);
    g_ts_config.mode = TS_MODE_DETECT;
    g_ts_config.detector = TS_DETECTOR_HYBRID;
    
    int shared_var = 0;
    
    /* Simulate Thread 1 writing */
    g_ts_current_tid = 1;
    ts_thread_info_t *t1 = ts_get_thread_info(1);
    t1->active = true;
    t1->locks_held = 0;
    vc_init(&t1->hb_clock);
    vc_init(&t1->st_clock);
    t1->hb_clock.clock[1] = 1;
    t1->st_clock.clock[1] = 1;
    
    detector_process_access(&shared_var, "shared_var", "test.c:10", true);
    
    /* Simulate Thread 2 writing concurrently without synchronization */
    g_ts_current_tid = 2;
    ts_thread_info_t *t2 = ts_get_thread_info(2);
    t2->active = true;
    t2->locks_held = 0;
    vc_init(&t2->hb_clock);
    vc_init(&t2->st_clock);
    t2->hb_clock.clock[2] = 1;
    t2->st_clock.clock[2] = 1;
    
    int before_confirmed = report_get_confirmed_count();
    detector_process_access(&shared_var, "shared_var", "test.c:20", true);
    int after_confirmed = report_get_confirmed_count();
    
    /* CONFIRMED race must be detected */
    ASSERT_EQ(after_confirmed, before_confirmed + 1);
}

int main(void) {
    ts_runtime_init();
    printf("=== Running Race Detector Unit Tests ===\n");
    RUN_TEST(test_detector_modes_configuration);
    RUN_TEST(test_detector_concurrent_write_write_race);
    
    TEST_REPORT();
    return g_tests_failed > 0 ? 1 : 0;
}
