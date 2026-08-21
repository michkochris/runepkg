#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "runepkg_util.h"

// satisfy dependencies for runepkg_util
bool g_verbose_mode = false;
bool g_debug_mode = false;
char *g_runepkg_db_dir = "/tmp";

void test_cmp(const char *v1, const char *v2, int expected) {
    int res = runepkg_util_compare_versions(v1, v2);
    int normalized_res = (res < 0) ? -1 : (res > 0 ? 1 : 0);
    int normalized_expected = (expected < 0) ? -1 : (expected > 0 ? 1 : 0);

    if (normalized_res == normalized_expected) {
        printf("[PASS] '%s' vs '%s': got %d\n", v1, v2, res);
    } else {
        printf("[FAIL] '%s' vs '%s': expected %d, got %d\n", v1, v2, expected, res);
    }
}

void test_constraint(const char *ver, const char *cons, int expected) {
    int res = runepkg_util_check_version_constraint(ver, cons);
    if (res == expected) {
        printf("[PASS] %s %s: got %d\n", ver, cons, res);
    } else {
        printf("[FAIL] %s %s: expected %d, got %d\n", ver, cons, expected, res);
    }
}

int main() {
    printf("--- Debian Version Comparison Tests ---\n");
    test_cmp("1.0~beta1", "1.0", -1);
    test_cmp("1.0", "1.0~beta1", 1);
    test_cmp("2.1.5+ds-2", "2~", 1);
    test_cmp("2~", "2.1.5+ds-2", -1);
    test_cmp("1.2-1ubuntu1", "1.2-1", 1);
    test_cmp("1:1.0", "0:2.0", 1);
    test_cmp("1:2:3", "1:2:3", 0);
    test_cmp("1:2:3", "1:2:2", 1);
    test_cmp("1:0:0", "0:1:1", 1);
    test_cmp("1.0-1", "1.0", 1);
    test_cmp("1.0", "1.0-1", -1);
    test_cmp("2.1.5+ds-2", "2.1.5+ds-2", 0);
    test_cmp("2.1.5+ds-2", "2.1.5+ds-1", 1);

    printf("\n--- Dependency Constraint Tests ---\n");
    // The specific failure case from the logs: glycin-loaders 2.1.5+ds-2 >= 2~
    test_constraint("2.1.5+ds-2", ">= 2~", 1);
    test_constraint("1.9.9", ">= 2~", 0);
    test_constraint("1.0~rc1", "< 1.0", 1);
    test_constraint("1.0", "> 1.0~rc1", 1);

    return 0;
}
