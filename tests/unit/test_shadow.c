#include "test_framework.h"
#include "shadow.h"

TEST_CASE(test_shadow_creation_and_lookup) {
    shadow_init();
    
    int var1 = 42;
    int var2 = 84;
    
    ts_shadow_entry_t *e1 = shadow_get_or_create(&var1, "var1");
    ASSERT_TRUE(e1 != NULL);
    ASSERT_EQ(e1->addr, (uintptr_t)&var1);
    ASSERT_STR_EQ(e1->var_name, "var1");
    
    ts_shadow_entry_t *e1_lookup = shadow_lookup(&var1);
    ASSERT_EQ(e1, e1_lookup);
    
    ts_shadow_entry_t *e2 = shadow_get_or_create(&var2, "var2");
    ASSERT_TRUE(e2 != NULL);
    ASSERT_TRUE(e1 != e2);
    ASSERT_EQ(e2->addr, (uintptr_t)&var2);
    ASSERT_STR_EQ(e2->var_name, "var2");
    
    shadow_shutdown();
}

TEST_CASE(test_shadow_free) {
    shadow_init();
    
    int *dynamic_var = (int *)malloc(sizeof(int));
    ts_shadow_entry_t *e = shadow_get_or_create(dynamic_var, "dynamic_var");
    ASSERT_TRUE(e != NULL);
    ASSERT_TRUE(shadow_lookup(dynamic_var) != NULL);
    
    shadow_free_entry(dynamic_var);
    ASSERT_TRUE(shadow_lookup(dynamic_var) == NULL);
    
    free(dynamic_var);
    shadow_shutdown();
}

int main(void) {
    printf("=== Running Shadow Memory Unit Tests ===\n");
    RUN_TEST(test_shadow_creation_and_lookup);
    RUN_TEST(test_shadow_free);
    
    TEST_REPORT();
    return g_tests_failed > 0 ? 1 : 0;
}
