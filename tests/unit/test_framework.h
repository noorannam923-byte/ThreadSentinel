#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;
static const char *g_current_test = NULL;

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"

#define TEST_CASE(name) static void name(bool *_test_passed)

#define ASSERT(expr) do { \
    if (!(expr)) { \
        printf("    " COLOR_RED "ASSERTION FAILED: %s (%s:%d)" COLOR_RESET "\n", \
               #expr, __FILE__, __LINE__); \
        *_test_passed = false; \
        return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    long long _va = (long long)(a); \
    long long _vb = (long long)(b); \
    if (_va != _vb) { \
        printf("    " COLOR_RED "ASSERTION FAILED: %s == %s (got %lld, expected %lld) (%s:%d)" COLOR_RESET "\n", \
               #a, #b, _va, _vb, __FILE__, __LINE__); \
        *_test_passed = false; \
        return; \
    } \
} while (0)

#define ASSERT_TRUE(expr) ASSERT(expr)
#define ASSERT_FALSE(expr) ASSERT(!(expr))

#define ASSERT_STR_EQ(a, b) do { \
    const char *_sa = (const char *)(a); \
    const char *_sb = (const char *)(b); \
    if (_sa == NULL || _sb == NULL || strcmp(_sa, _sb) != 0) { \
        printf("    " COLOR_RED "ASSERTION FAILED: \"%s\" == \"%s\" (%s:%d)" COLOR_RESET "\n", \
               _sa ? _sa : "NULL", _sb ? _sb : "NULL", __FILE__, __LINE__); \
        *_test_passed = false; \
        return; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    g_current_test = #fn; \
    bool _passed = true; \
    printf(COLOR_CYAN "[RUN ]" COLOR_RESET " %s\n", #fn); \
    fn(&_passed); \
    if (_passed) { \
        printf(COLOR_GREEN "[PASS]" COLOR_RESET " %s\n", #fn); \
    } else { \
        printf(COLOR_RED "[FAIL]" COLOR_RESET " %s\n", #fn); \
        g_tests_failed++; \
    } \
} while (0)

#define TEST_REPORT() do { \
    printf("\n----------------------------------------\n"); \
    printf("Tests run: %d, Passed: %d, Failed: %d\n", \
           g_tests_run, g_tests_run - g_tests_failed, g_tests_failed); \
    if (g_tests_failed == 0) { \
        printf(COLOR_GREEN "ALL TESTS PASSED!" COLOR_RESET "\n"); \
    } else { \
        printf(COLOR_RED "SOME TESTS FAILED!" COLOR_RESET "\n"); \
    } \
    printf("----------------------------------------\n"); \
} while (0)

#endif /* TEST_FRAMEWORK_H */
