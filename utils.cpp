#include "utils.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <chrono>
#include <cstdio>
#include <sched.h>
#include <cerrno>

bool is_logging_enabled() {
    const char* log_env = getenv("GATHER_CXI_LOG");
    return log_env != nullptr && strcmp(log_env, "1") == 0;
}

bool is_json_enabled() {
    const char* json_env = getenv("GATHER_CXI_JSON");
    return json_env != nullptr && strcmp(json_env, "1") == 0;
}

bool is_detailed_enabled() {
    const char* detailed_env = getenv("GATHER_CXI_DETAILED");
    return detailed_env != nullptr && strcmp(detailed_env, "1") == 0;
}

int get_sample_interval() {
    const char* interval_env = getenv("GATHER_CXI_INTERVAL");
    if (interval_env == nullptr) return 100; // default: 100ms
    char* endptr = nullptr;
    long interval = strtol(interval_env, &endptr, 10);
    if (endptr == interval_env || *endptr != '\0' || interval <= 0) {
        return 100; // default on invalid input
    }
    return static_cast<int>(interval);
}

CounterLevel get_counter_level() {
    const char* level_env = getenv("COUNTER_LEVEL");
    if (level_env == nullptr) return DEFAULT;
    char* endptr = nullptr;
    long level = strtol(level_env, &endptr, 10);
    if (endptr == level_env || *endptr != '\0' || level < 0 || level > 5) {
        return DEFAULT;
    }
    return static_cast<CounterLevel>(level);
}

bool apply_cpu_pin() {
    const char* pin_env = getenv("GATHER_CXI_CPU_PIN");
    if (!pin_env || pin_env[0] == '\0') return false;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    std::string pin_str(pin_env);

    if (pin_str == "last") {
        // Pin to the last 2 cores on the system
        long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (ncpus < 4) {
            std::cerr << "[cpu_pin] Only " << ncpus << " cores, skipping pin" << std::endl;
            return false;
        }
        CPU_SET(ncpus - 1, &cpuset);
        CPU_SET(ncpus - 2, &cpuset);
        if (is_logging_enabled()) {
            std::cerr << "[cpu_pin] Pinning sidecar to cores "
                      << (ncpus - 2) << "-" << (ncpus - 1) << std::endl;
        }
    } else {
        // Parse "N" or "N-M" format
        auto dash = pin_str.find('-');
        if (dash != std::string::npos) {
            int lo = std::stoi(pin_str.substr(0, dash));
            int hi = std::stoi(pin_str.substr(dash + 1));
            for (int c = lo; c <= hi; ++c) CPU_SET(c, &cpuset);
            if (is_logging_enabled())
                std::cerr << "[cpu_pin] Pinning sidecar to cores " << lo << "-" << hi << std::endl;
        } else {
            int core = std::stoi(pin_str);
            CPU_SET(core, &cpuset);
            if (is_logging_enabled())
                std::cerr << "[cpu_pin] Pinning sidecar to core " << core << std::endl;
        }
    }

    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        std::cerr << "[cpu_pin] sched_setaffinity failed: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

void reset_cpu_affinity() {
    long ncpus = sysconf(_SC_NPROCESSORS_ONLN);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (long c = 0; c < ncpus; ++c) CPU_SET(c, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
}

CommandResult run_command(const std::vector<std::string>& args) {
    CommandResult result;
    result.execution_time = -1.0;

    if (args.empty()) {
        std::cerr << "Error: No command provided." << std::endl;
        return result;
    }

    std::vector<char*> c_args;
    for (const auto& arg : args) {
        c_args.push_back(const_cast<char*>(arg.c_str()));
    }
    c_args.push_back(nullptr);

    // Create pipe for capturing stdout
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        std::cerr << "Error: Failed to create pipe" << std::endl;
        return result;
    }

    auto start = std::chrono::high_resolution_clock::now();
    pid_t pid = fork();
    if (pid == 0) {
        // Child process — reset CPU affinity so workload isn't constrained
        reset_cpu_affinity();
        close(pipefd[0]);  // Close read end
        dup2(pipefd[1], STDOUT_FILENO);  // Redirect stdout to pipe
        close(pipefd[1]);
        execvp(c_args[0], c_args.data());
        std::cerr << "Error: Failed to execute command '" << c_args[0] << "'" << std::endl;
        exit(1);
    } else if (pid > 0) {
        // Parent process
        close(pipefd[1]);  // Close write end

        // Read stdout from child and parse for timestamps
        FILE* fp = fdopen(pipefd[0], "r");
        if (fp) {
            // Check if CSV mode to suppress output
            const char* output_format = std::getenv("GATHER_CXI_OUTPUT_FORMAT");
            bool is_csv_mode = (output_format != nullptr &&
                               (strstr(output_format, "csv") != nullptr || strstr(output_format, "CSV") != nullptr));

            char line[4096];
            while (fgets(line, sizeof(line), fp)) {
                // In CSV mode, suppress child output (it would contaminate CSV)
                // In other modes, pass through to stdout
                if (!is_csv_mode) {
                    printf("%s", line);
                }

                // Parse line for timestamp and latency
                // Format: "  <time_us>  <algBw>  <busBw>  <errors>  <timestamp>"
                // where timestamp is the LAST field (Unix time > 1000000000)
                // and latency (time_us) is the FIRST field
                if (line[0] == ' ' && line[1] == ' ') {
                    // Work on a copy for parsing
                    char line_copy[4096];
                    strncpy(line_copy, line, sizeof(line_copy));
                    line_copy[sizeof(line_copy)-1] = '\0';

                    // Find all tokens
                    std::vector<char*> tokens;
                    char* token = strtok(line_copy, " \t\n");
                    while (token != nullptr) {
                        tokens.push_back(token);
                        token = strtok(nullptr, " \t\n");
                    }

                    // Need at least 5 tokens: <time> <algBw> <busBw> <errors> <timestamp>
                    if (tokens.size() >= 5) {
                        char* last_token = tokens[tokens.size() - 1];       // timestamp
                        char* latency_token = tokens[tokens.size() - 5];     // time_us

                        char* endptr1;
                        char* endptr2;
                        double latency_us = strtod(latency_token, &endptr1);
                        double timestamp = strtod(last_token, &endptr2);

                        // Validate:
                        // - Last token must be a valid Unix timestamp (> 1000000000, after year 2001)
                        // - Latency must be a positive number < 1000000000 us (~16 min)
                        if (endptr2 != last_token && *endptr2 == '\0' && timestamp > 1000000000.0 &&
                            endptr1 != latency_token && *endptr1 == '\0' && latency_us > 0.0 && latency_us < 1000000000.0) {
                            result.latency_timestamps.push_back({timestamp, latency_us});
                            // Repeat number is the iteration number (1-indexed)
                            result.repeat_numbers.push_back(static_cast<int>(result.latency_timestamps.size()));
                            if (is_logging_enabled()) {
                                fprintf(stderr, "[DEBUG] Captured timestamp: %.3f, latency: %.2f us, repeat: %d\n",
                                        timestamp, latency_us, result.repeat_numbers.back());
                            }
                        }
                    }
                }
            }
            fclose(fp);
        } else {
            close(pipefd[0]);
        }

        int status = 0;
        waitpid(pid, &status, 0);
        auto end = std::chrono::high_resolution_clock::now();
        result.execution_time = std::chrono::duration<double>(end - start).count();
        return result;
    } else {
        std::cerr << "Error: Fork failed." << std::endl;
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }
}

std::vector<std::string> get_node_list_from_scontrol() {
    /*
     * Returns a vector of all node hostnames in the SLURM allocation
     */
    std::vector<std::string> nodes;
    FILE* fp = popen("scontrol show hostnames", "r");
    if (!fp) return nodes;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) {
        std::string node(buf);
        if (!node.empty() && node.back() == '\n') node.pop_back();
        if (!node.empty()) nodes.push_back(node);
    }
    pclose(fp);
    return nodes;
}

std::string get_previous_node(const std::vector<std::string>& nodes, long procid) {
    /*
     * Returns the hostname of the previous node in the chain
     */
    if (procid == 0) return ""; // First node has no previous
    if (procid > 0 && procid < (long)nodes.size()) return nodes[procid - 1];
    return "";
}
