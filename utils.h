#ifndef UTILS_H
#define UTILS_H

#include <vector>
#include <string>

// Enum for counter verbosity levels
enum CounterLevel {
    QUIET = 0,
    DEFAULT = 1,
    SUMMARY = 2,
    ON_ERROR = 3,
    ALL_ON_ERROR = 4,
    ALL = 5
};

// Checks if verbose logging is enabled via the GATHER_CXI_LOG environment variable.
bool is_logging_enabled();

// Checks if JSON output is enabled via the GATHER_CXI_JSON environment variable.
bool is_json_enabled();

// Checks if detailed output is enabled via the GATHER_CXI_DETAILED environment variable.
bool is_detailed_enabled();

// Gets the counter level from the COUNTER_LEVEL environment variable.
CounterLevel get_counter_level();

// Gets the sampling interval from the GATHER_CXI_INTERVAL environment variable (default: 100ms).
int get_sample_interval();

// Command execution result
struct CommandResult {
    double execution_time;  // Total execution time in seconds
    std::vector<std::pair<double, double>> latency_timestamps;  // (timestamp, latency_us)
    std::vector<int> repeat_numbers;  // iteration number (1 to CYCLES) for each latency measurement
};

// Executes a command with arguments and measures its wall-clock execution time.
CommandResult run_command(const std::vector<std::string>& args);

// Retrieves the list of all node hostnames in the current SLURM job allocation.
std::vector<std::string> get_node_list_from_scontrol();

// Determines the hostname of the previous node in the chain aggregation topology.
std::string get_previous_node(const std::vector<std::string>& nodes, long procid);

// Pin the current process to specific CPU cores to reduce interference.
// Reads GATHER_CXI_CPU_PIN env var:
//   "last"     - pin to last 2 cores on the system
//   "N"        - pin to core N
//   "N-M"      - pin to cores N through M
//   unset/empty - no pinning
// Returns true if pinning was applied.
bool apply_cpu_pin();

// Reset CPU affinity to all available cores (called in child before exec).
void reset_cpu_affinity();

#endif
