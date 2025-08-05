// Simple wrapper to run memory tests without conflicting with CLI main
#include <stdbool.h>

// Define global variable for testing
bool g_verbose_mode = true;

extern int memory_test_main();

int main() {
    return memory_test_main();
}
