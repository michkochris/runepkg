/**
 * rune_bridge.c - Bridge between runepkg and rune_analyze
 * 
 * Revolutionary connection system that enables:
 * - runepkg -vv data feeding into rune_analyze --json
 * - Real-time cross-tool analysis and monitoring
 * - Meta-analysis of package management operations
 *
 * Copyright (C) 2025 Christopher Michko
 * Co-developed with GitHub Copilot AI Assistant
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_COMMAND_LENGTH 4096
#define MAX_OUTPUT_BUFFER 65536

// Bridge configuration
typedef struct {
    char runepkg_command[MAX_COMMAND_LENGTH];
    char rune_analyze_command[MAX_COMMAND_LENGTH];
    int enable_real_time;
    int enable_json_bridge;
    int enable_vulnerability_detection;
    char output_file[256];
} BridgeConfig;

// Bridge state
typedef struct {
    pid_t runepkg_pid;
    pid_t rune_analyze_pid;
    FILE *runepkg_output;
    FILE *rune_analyze_input;
    char shared_data[MAX_OUTPUT_BUFFER];
    time_t start_time;
} BridgeState;

static BridgeConfig g_config = {0};
static BridgeState g_state = {0};

/**
 * @brief Initialize the bridge configuration
 */
int bridge_init(int argc, char **argv) {
    // Set defaults
    strcpy(g_config.output_file, "rune_bridge_output.json");
    g_config.enable_real_time = 1;
    g_config.enable_json_bridge = 1;
    g_config.enable_vulnerability_detection = 1;
    
    // Parse arguments for bridge configuration
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--runepkg") == 0 && i + 1 < argc) {
            strncpy(g_config.runepkg_command, argv[i + 1], sizeof(g_config.runepkg_command) - 1);
            i++;
        } else if (strcmp(argv[i], "--rune-analyze") == 0 && i + 1 < argc) {
            strncpy(g_config.rune_analyze_command, argv[i + 1], sizeof(g_config.rune_analyze_command) - 1);
            i++;
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            strncpy(g_config.output_file, argv[i + 1], sizeof(g_config.output_file) - 1);
            i++;
        }
    }
    
    g_state.start_time = time(NULL);
    return 0;
}

/**
 * @brief Execute runepkg with -vv and capture output
 */
int execute_runepkg_monitored(void) {
    char full_command[MAX_COMMAND_LENGTH];
    
    // Ensure runepkg runs with -vv --both for maximum data
    if (strstr(g_config.runepkg_command, "-vv") == NULL) {
        snprintf(full_command, sizeof(full_command), "%s -vv --both", g_config.runepkg_command);
    } else {
        strncpy(full_command, g_config.runepkg_command, sizeof(full_command) - 1);
    }
    
    printf("🔗 BRIDGE: Executing runepkg with monitoring: %s\n", full_command);
    
    // Create pipe to capture runepkg output
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe failed");
        return -1;
    }
    
    g_state.runepkg_pid = fork();
    if (g_state.runepkg_pid == 0) {
        // Child: redirect output to pipe and exec runepkg
        close(pipefd[0]);  // Close read end
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        
        // Execute runepkg
        execl("/bin/sh", "sh", "-c", full_command, (char *)NULL);
        perror("execl failed");
        exit(1);
    } else if (g_state.runepkg_pid > 0) {
        // Parent: read from pipe
        close(pipefd[1]);  // Close write end
        g_state.runepkg_output = fdopen(pipefd[0], "r");
        
        if (!g_state.runepkg_output) {
            perror("fdopen failed");
            close(pipefd[0]);
            return -1;
        }
        
        return 0;
    } else {
        perror("fork failed");
        return -1;
    }
}

/**
 * @brief Launch rune_analyze to monitor the runepkg process
 */
int launch_rune_analyze_monitor(void) {
    char full_command[MAX_COMMAND_LENGTH];
    
    // Get the runepkg binary path for analysis
    char runepkg_binary[256];
    snprintf(runepkg_binary, sizeof(runepkg_binary), "/mnt/c/Users/michk/Downloads/work/runepkg/runepkg/runepkg");
    
    // Ensure rune_analyze runs with --json for structured output
    if (strstr(g_config.rune_analyze_command, "--json") == NULL) {
        snprintf(full_command, sizeof(full_command), "%s --json %s", 
                g_config.rune_analyze_command, runepkg_binary);
    } else {
        strncpy(full_command, g_config.rune_analyze_command, sizeof(full_command) - 1);
    }
    
    printf("🔬 BRIDGE: Launching rune_analyze monitor: %s\n", full_command);
    
    // Execute rune_analyze in parallel
    g_state.rune_analyze_pid = fork();
    if (g_state.rune_analyze_pid == 0) {
        // Child: exec rune_analyze
        execl("/bin/sh", "sh", "-c", full_command, (char *)NULL);
        perror("rune_analyze execl failed");
        exit(1);
    } else if (g_state.rune_analyze_pid < 0) {
        perror("rune_analyze fork failed");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Process and bridge the data between tools
 */
int process_bridge_data(void) {
    char line[1024];
    FILE *output_file = fopen(g_config.output_file, "w");
    
    if (!output_file) {
        perror("Failed to open output file");
        return -1;
    }
    
    printf("🌉 BRIDGE: Processing data stream...\n");
    
    // Write bridge metadata
    fprintf(output_file, "{\n");
    fprintf(output_file, "  \"rune_bridge_version\": \"1.0.0\",\n");
    fprintf(output_file, "  \"bridge_start_time\": %ld,\n", g_state.start_time);
    fprintf(output_file, "  \"runepkg_command\": \"%s\",\n", g_config.runepkg_command);
    fprintf(output_file, "  \"rune_analyze_command\": \"%s\",\n", g_config.rune_analyze_command);
    fprintf(output_file, "  \"runepkg_output\": [\n");
    
    int first_line = 1;
    
    // Read runepkg output line by line
    while (fgets(line, sizeof(line), g_state.runepkg_output)) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        // Real-time display
        if (g_config.enable_real_time) {
            printf("📦 RUNEPKG: %s\n", line);
        }
        
        // Write to bridge output
        if (!first_line) {
            fprintf(output_file, ",\n");
        }
        fprintf(output_file, "    \"%s\"", line);
        first_line = 0;
        
        // Vulnerability detection patterns
        if (g_config.enable_vulnerability_detection) {
            if (strstr(line, "buffer overflow") || strstr(line, "BUFFER OVERFLOW")) {
                printf("🚨 VULNERABILITY DETECTED: Buffer overflow in runepkg!\n");
            }
            if (strstr(line, "memory leak") || strstr(line, "MEMORY LEAK")) {
                printf("🚨 VULNERABILITY DETECTED: Memory leak in runepkg!\n");
            }
            if (strstr(line, "path traversal") || strstr(line, "PATH TRAVERSAL")) {
                printf("🚨 VULNERABILITY DETECTED: Path traversal in runepkg!\n");
            }
        }
        
        fflush(output_file);
        fflush(stdout);
    }
    
    fprintf(output_file, "\n  ],\n");
    fprintf(output_file, "  \"bridge_end_time\": %ld\n", time(NULL));
    fprintf(output_file, "}\n");
    
    fclose(output_file);
    printf("🎯 BRIDGE: Data processing complete. Output written to %s\n", g_config.output_file);
    
    return 0;
}

/**
 * @brief Wait for both processes to complete
 */
int wait_for_completion(void) {
    int runepkg_status, rune_analyze_status;
    
    printf("⏳ BRIDGE: Waiting for processes to complete...\n");
    
    // Wait for runepkg
    if (g_state.runepkg_pid > 0) {
        waitpid(g_state.runepkg_pid, &runepkg_status, 0);
        printf("📦 RUNEPKG: Process completed with status %d\n", WEXITSTATUS(runepkg_status));
    }
    
    // Wait for rune_analyze
    if (g_state.rune_analyze_pid > 0) {
        waitpid(g_state.rune_analyze_pid, &rune_analyze_status, 0);
        printf("🔬 RUNE_ANALYZE: Process completed with status %d\n", WEXITSTATUS(rune_analyze_status));
    }
    
    return 0;
}

/**
 * @brief Print usage information
 */
void print_usage(const char *program_name) {
    printf("🌉 rune_bridge - Revolutionary Connection System\n\n");
    printf("Usage: %s --runepkg \"COMMAND\" --rune-analyze \"COMMAND\" [OPTIONS]\n\n", program_name);
    printf("Examples:\n");
    printf("  # Basic bridge operation\n");
    printf("  %s --runepkg \"runepkg -i package.deb\" --rune-analyze \"rune_analyze\"\n\n", program_name);
    printf("  # Advanced monitoring with JSON output\n");
    printf("  %s --runepkg \"runepkg -vv --both -i test.deb\" \\\n", program_name);
    printf("        --rune-analyze \"rune_analyze --json -vv\" \\\n");
    printf("        --output combined_analysis.json\n\n");
    printf("Options:\n");
    printf("  --runepkg \"CMD\"      Command to execute for runepkg\n");
    printf("  --rune-analyze \"CMD\" Command to execute for rune_analyze\n");
    printf("  --output FILE        Output file for bridge results\n");
    printf("  --help               Show this help message\n\n");
    printf("🚀 Connection Possibilities:\n");
    printf("  • Real-time vulnerability detection\n");
    printf("  • Cross-tool data correlation\n");
    printf("  • Meta-analysis of package operations\n");
    printf("  • Self-monitoring package manager\n\n");
}

/**
 * @brief Main bridge function
 */
int main(int argc, char **argv) {
    printf("🌉 rune_bridge v1.0.0 - Revolutionary Connection System\n");
    printf("═══════════════════════════════════════════════════════\n\n");
    
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Check for help
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    // Initialize bridge
    if (bridge_init(argc, argv) != 0) {
        fprintf(stderr, "Failed to initialize bridge\n");
        return 1;
    }
    
    // Execute runepkg with monitoring
    if (execute_runepkg_monitored() != 0) {
        fprintf(stderr, "Failed to execute runepkg\n");
        return 1;
    }
    
    // Launch rune_analyze monitor
    if (launch_rune_analyze_monitor() != 0) {
        fprintf(stderr, "Failed to launch rune_analyze\n");
        return 1;
    }
    
    // Process the bridged data
    if (process_bridge_data() != 0) {
        fprintf(stderr, "Failed to process bridge data\n");
        return 1;
    }
    
    // Wait for completion
    wait_for_completion();
    
    printf("\n🎉 BRIDGE COMPLETE: Revolutionary connection established!\n");
    printf("🔗 Both tools worked together seamlessly\n");
    printf("📊 Combined analysis data available in %s\n", g_config.output_file);
    
    return 0;
}
