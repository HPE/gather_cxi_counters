#ifndef COUNTER_AGGREGATION_H
#define COUNTER_AGGREGATION_H

#include <vector>
#include <string>
#include <map>
#include <cstdint>

// Output format options
enum class OutputFormat {
    TEXT,      // Human-readable table (default)
    JSON,      // Detailed JSON with per-node time series
    CSV_TALL,  // Tall-format CSV (multiple rows per timestamp)
    CSV_WIDE   // Wide-format CSV (one row per timestamp, 800+ columns)
};

// Struct for aligned CSV sample (one row per NIC per timestamp - used for tall format)
struct AlignedSample {
    double timestamp;      // UNIX timestamp aligned to nearest 100ms
    int repeat;            // Iteration number (1 to CYCLES, or 0 before any allreduce calls)
    std::string hostname;  // Node hostname
    std::string nic_name;  // NIC identifier (e.g., "hsn0")
    std::vector<uint64_t> counter_values;  // Counter values for this NIC
    double allreduce_latency_us;      // AllReduce latency in microseconds, averaged over bucket (NaN if not available)
    double max_allreduce_latency_us;  // Max allreduce latency in the bucket (equals avg when bucket has one entry)
};

// Struct to hold statistics for a counter across nodes
struct CounterStats {
    uint64_t min;   // Minimum value
    uint64_t max;   // Maximum value
    double mean;    // Mean value
    uint64_t sum;   // Sum of values
    double time;    // Time interval (seconds)
};

// Struct for time-series sample
struct TimeSample {
    double timestamp;  // UNIX timestamp in seconds with millisecond precision (3 decimal places)
    std::map<std::string, std::vector<uint64_t>> nic_counters;  // per-NIC values (deprecated, kept for backward compat)
    
    // NEW: Per-source metric data from plugin architecture
    // source_name -> samples from that source
    std::map<std::string, std::map<std::string, std::map<std::string, uint64_t>>> sources;
    // Breakdown: sources[source_name][entity_id][metric_name] = value
    // Example: sources["nic"]["hsn0"]["hni_rx_paused_0"] = 12345
    //          sources["perf"]["node001"]["cache_misses"] = 123456
};

// Struct for per-node time-series data
struct NodeTimeSeries {
    std::string hostname;
    long procid;
    double execution_time;
    std::vector<TimeSample> samples;
    std::vector<std::pair<double, double>> latency_timestamps;  // pairs of (timestamp, latency_us)
    std::vector<int> repeat_numbers;  // iteration numbers corresponding to latency_timestamps
};

// Struct to hold aggregated statistics for all nodes (used in chain aggregation)
struct AggregatedStats {
    std::vector<std::vector<uint64_t>> all_initial; // Initial counters for all nodes
    std::vector<std::vector<uint64_t>> all_final;   // Final counters for all nodes
    std::vector<std::string> hostnames;             // Hostnames for all nodes
    std::vector<long> procids;                      // Process IDs for all nodes
    std::vector<double> times;                      // Execution times for all nodes
    std::vector<NodeTimeSeries> time_series;        // NEW: per-node time-series
};

// Compute min, max, mean, sum for a counter across all nodes
CounterStats get_counter_stats(const std::map<long, std::vector<uint64_t>>& initial_counters,
                              const std::map<long, std::vector<uint64_t>>& final_counters,
                              size_t idx,
                              double time_interval);

// Print a summary table of statistics for all counters
void print_summary(const std::map<long, std::vector<uint64_t>>& initial_counters,
                        const std::map<long, std::vector<uint64_t>>& final_counters,
                        double time_interval);

// Unified handler for all node types in chain aggregation
void node_handler(bool is_first, bool is_last, long procid, long node_count, int cmd_start_idx, int argc, char* argv[], const std::string& experiment_name = "");

#endif
