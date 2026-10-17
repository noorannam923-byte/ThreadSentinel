#include "test_framework.h"
#include "vector_clock.h"
#include <string.h>

TEST_CASE(test_vc_init) {
    vector_clock_t vc;
    vc.clock[0] = 99;
    vc.clock[10] = 5;
    
    vc_init(&vc);
    for (int i = 0; i < TS_MAX_THREADS; i++) {
        ASSERT_EQ(vc.clock[i], 0);
    }
}

TEST_CASE(test_vc_increment) {
    vector_clock_t vc;
    vc_init(&vc);
    
    vc_increment(&vc, 0);
    ASSERT_EQ(vc.clock[0], 1);
    ASSERT_EQ(vc.clock[1], 0);
    
    vc_increment(&vc, 0);
    ASSERT_EQ(vc.clock[0], 2);
    
    vc_increment(&vc, 5);
    ASSERT_EQ(vc.clock[5], 1);
}

TEST_CASE(test_vc_copy) {
    vector_clock_t src, dst;
    vc_init(&src);
    vc_init(&dst);
    
    src.clock[0] = 10;
    src.clock[3] = 42;
    
    vc_copy(&dst, &src);
    ASSERT_EQ(dst.clock[0], 10);
    ASSERT_EQ(dst.clock[3], 42);
    ASSERT_EQ(dst.clock[1], 0);
}

TEST_CASE(test_vc_join) {
    vector_clock_t a, b;
    vc_init(&a);
    vc_init(&b);
    
    a.clock[0] = 5;
    a.clock[1] = 2;
    a.clock[2] = 0;
    
    b.clock[0] = 3;
    b.clock[1] = 7;
    b.clock[2] = 4;
    
    vc_join(&a, &b);
    ASSERT_EQ(a.clock[0], 5);
    ASSERT_EQ(a.clock[1], 7);
    ASSERT_EQ(a.clock[2], 4);
}

TEST_CASE(test_vc_happens_before) {
    vector_clock_t a, b, c;
    vc_init(&a);
    vc_init(&b);
    vc_init(&c);
    
    a.clock[0] = 2;
    a.clock[1] = 3;
    
    /* b is strictly after a */
    b.clock[0] = 2;
    b.clock[1] = 4;
    ASSERT_TRUE(vc_happens_before(&a, &b));
    ASSERT_FALSE(vc_happens_before(&b, &a));
    
    /* Identical clocks are not strictly happens-before */
    ASSERT_FALSE(vc_happens_before(&a, &a));
    
    /* c is concurrent with a: c[0] > a[0] but c[1] < a[1] */
    c.clock[0] = 3;
    c.clock[1] = 1;
    ASSERT_FALSE(vc_happens_before(&a, &c));
    ASSERT_FALSE(vc_happens_before(&c, &a));
}

TEST_CASE(test_vc_happens_before_epoch) {
    vector_clock_t b;
    vc_init(&b);
    b.clock[2] = 5;
    
    /* Prior event in thread 2 at epoch 3 */
    ASSERT_TRUE(vc_happens_before_epoch(2, 3, &b));
    /* Prior event at epoch 5 */
    ASSERT_TRUE(vc_happens_before_epoch(2, 5, &b));
    /* Event at epoch 6 did not happen before b */
    ASSERT_FALSE(vc_happens_before_epoch(2, 6, &b));
    /* Thread 1 has epoch 0 in b */
    ASSERT_FALSE(vc_happens_before_epoch(1, 1, &b));
}

TEST_CASE(test_vc_to_string) {
    vector_clock_t vc;
    vc_init(&vc);
    char buf[128];
    
    vc_to_string(&vc, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "[]");
    
    vc.clock[0] = 1;
    vc.clock[1] = 3;
    vc_to_string(&vc, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "[1,3]");
}

int main(void) {
    printf("=== Running Vector Clock Unit Tests ===\n");
    RUN_TEST(test_vc_init);
    RUN_TEST(test_vc_increment);
    RUN_TEST(test_vc_copy);
    RUN_TEST(test_vc_join);
    RUN_TEST(test_vc_happens_before);
    RUN_TEST(test_vc_happens_before_epoch);
    RUN_TEST(test_vc_to_string);
    
    TEST_REPORT();
    return g_tests_failed > 0 ? 1 : 0;
}
