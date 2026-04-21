#include "counter_aggregation.h"
#include "counter_collection.h"
#include "utils.h"
#include "zmq_transfer.h"
#include "metric_registry.h"
#include "json/json.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <limits>
#include <ctime>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>

CounterStats get_counter_stats(const std::map<long, std::vector<uint64_t>>& initial_counters,
                              const std::map<long, std::vector<uint64_t>>& final_counters,
                              size_t idx,
                              double time_interval) {
    /*
     * For each node, compute the difference for the given counter index
     * Then compute min, max, mean, sum across all nodes
     */
    std::vector<uint64_t> diffs;
    for (const auto& kv : initial_counters) {
        long procid = kv.first;
        auto it_final = final_counters.find(procid);
        if (it_final != final_counters.end()) {
            uint64_t initial = kv.second[idx];
            uint64_t final = it_final->second[idx];
            uint64_t diff = subtract_56_bit_integers(final, initial);
            diffs.push_back(diff);
        }
    }
    uint64_t min = diffs.empty() ? 0 : diffs[0];
    uint64_t max = diffs.empty() ? 0 : diffs[0];
    uint64_t sum = 0;
    for (auto v : diffs) {
        if (v < min) min = v;
        if (v > max) max = v;
        sum += v;
    }
    double mean = diffs.empty() ? 0.0 : static_cast<double>(sum) / diffs.size();
    return {min, max, mean, sum, time_interval};
}

static void print_json_summary(const std::map<long, std::vector<uint64_t>>& initial_counters,
                        const std::map<long, std::vector<uint64_t>>& final_counters,
                        double time_interval) {
    nlohmann::json j;
    j["time_interval"] = time_interval;
    j["counters"] = nlohmann::json::object();
    for (size_t i = 0; i < counter_names.size(); ++i) {
        CounterStats stats = get_counter_stats(initial_counters, final_counters, i, time_interval);
        if (stats.sum > 0) {
            double min_sec = stats.time > 0 ? static_cast<double>(stats.min) / stats.time : 0.0;
            double mean_sec = stats.time > 0 ? stats.mean / stats.time : 0.0;
            double max_sec = stats.time > 0 ? static_cast<double>(stats.max) / stats.time : 0.0;
            j["counters"][counter_names[i]] = {
                {"min", stats.min},
                {"min_per_sec", min_sec},
                {"mean", stats.mean},
                {"mean_per_sec", mean_sec},
                {"max", stats.max},
                {"max_per_sec", max_sec},
                {"sum", stats.sum}
            };
        }
    }
    std::cout << j.dump(4) << std::endl;
}

static void print_human_summary(const std::map<long, std::vector<uint64_t>>& initial_counters,
                        const std::map<long, std::vector<uint64_t>>& final_counters,
                        double time_interval) {
    /*
     * Prints a formatted table of min, mean, max, and per-second rates for each counter
     */
    std::cout << "Slingshot CXI Counter Summary:" << std::endl;
    std::cout
        << std::left << std::setw(46) << "Counter"
        << std::right << std::setw(12) << "Min"
        << std::setw(12) << "(/s)"
        << std::setw(12) << "Mean"
        << std::setw(12) << "(/s)"
        << std::setw(12) << "Max"
        << std::setw(12) << "(/s)"
        << std::endl;
    for (size_t i = 0; i < counter_names.size(); ++i) {
        CounterStats stats = get_counter_stats(initial_counters, final_counters, i, time_interval);
        if (stats.sum > 0) {
            double min_sec = stats.time > 0 ? static_cast<double>(stats.min) / stats.time : 0.0;
            double mean_sec = stats.time > 0 ? stats.mean / stats.time : 0.0;
            double max_sec = stats.time > 0 ? static_cast<double>(stats.max) / stats.time : 0.0;
            std::cout
                << std::left << std::setw(46) << counter_names[i]
                << std::right << std::setw(12) << stats.min
                << std::setw(12) << std::fixed << std::setprecision(1) << min_sec
                << std::setw(12) << static_cast<uint64_t>(stats.mean)
                << std::setw(12) << std::fixed << std::setprecision(1) << mean_sec
                << std::setw(12) << stats.max
                << std::setw(12) << std::fixed << std::setprecision(1) << max_sec
                << std::endl;
        }
    }
}

static void print_detailed_json_summary(const AggregatedStats& agg, const std::vector<std::string>& cmd_args) {
    nlohmann::json j;
    j["version"] = "2.0";

    // Metadata
    j["metadata"] = nlohmann::json::object();
    std::string command_str = "";
    for (size_t i = 0; i < cmd_args.size(); ++i) {
        if (i > 0) command_str += " ";
        command_str += cmd_args[i];
    }
    j["metadata"]["command"] = command_str;
    j["metadata"]["total_time_seconds"] = std::accumulate(agg.times.begin(), agg.times.end(), 0.0) / agg.times.size();
    j["metadata"]["sample_interval_ms"] = get_sample_interval();
    j["metadata"]["counter_names"] = counter_names;
    j["metadata"]["node_count"] = agg.hostnames.size();

    // Nodes data
    j["nodes"] = nlohmann::json::array();
    if (!agg.time_series.empty()) {
        // Emit nodes based on time_series entries (per-node, per-NIC)
        for (const auto& nts : agg.time_series) {
            nlohmann::json node;
            node["hostname"] = nts.hostname;
            node["procid"] = nts.procid;
            node["execution_time_seconds"] = nts.execution_time;
            node["nics"] = nlohmann::json::array();
            std::map<std::string, nlohmann::json> nic_objs;
            for (const auto& sample : nts.samples) {
                for (const auto& kv : sample.nic_counters) {
                    auto it = nic_objs.find(kv.first);
                    if (it == nic_objs.end()) {
                        nlohmann::json nobj;
                        nobj["name"] = kv.first;
                        nobj["samples"] = nlohmann::json::array();
                        nobj["deltas"] = nlohmann::json::object();
                        nic_objs[kv.first] = nobj;
                    }
                    nlohmann::json s;
                    s["timestamp"] = sample.timestamp;
                    s["counters"] = kv.second;
                    nic_objs[kv.first]["samples"].push_back(s);
                }
            }
            // compute deltas using first and last samples
            for (auto& kv : nic_objs) {
                auto& nobj = kv.second;
                auto& samples = nobj["samples"];
                if (samples.size() >= 2) {
                    std::vector<uint64_t> first = samples.front()["counters"].get<std::vector<uint64_t>>();
                    std::vector<uint64_t> last = samples.back()["counters"].get<std::vector<uint64_t>>();
                    for (size_t k = 0; k < counter_names.size(); ++k) {
                        uint64_t delta = subtract_56_bit_integers(last[k], first[k]);
                        if (delta > 0) nobj["deltas"][counter_names[k]] = delta;
                    }
                }
                node["nics"].push_back(nobj);
            }
            j["nodes"].push_back(node);
        }
    } else {
        // Fallback to hostnames/aggregated behavior
        for (size_t i = 0; i < agg.hostnames.size(); ++i) {
            nlohmann::json node;
            node["hostname"] = agg.hostnames[i];
            node["procid"] = agg.procids[i];
            node["execution_time_seconds"] = agg.times[i];

            nlohmann::json nic;
            nic["name"] = "aggregated";
            nic["samples"] = nlohmann::json::array();
            // For backward compatibility when no time-series data exists,
            // we can't provide absolute timestamps, so keep as relative
            nlohmann::json initial_sample;
            initial_sample["timestamp"] = 0.0;
            initial_sample["counters"] = agg.all_initial[i];
            nic["samples"].push_back(initial_sample);
            nlohmann::json final_sample;
            final_sample["timestamp"] = agg.times[i];
            final_sample["counters"] = agg.all_final[i];
            nic["samples"].push_back(final_sample);
            nic["deltas"] = nlohmann::json::object();
            for (size_t k = 0; k < counter_names.size(); ++k) {
                uint64_t delta = subtract_56_bit_integers(agg.all_final[i][k], agg.all_initial[i][k]);
                if (delta > 0) nic["deltas"][counter_names[k]] = delta;
            }
            node["nics"] = nlohmann::json::array();
            node["nics"].push_back(nic);
            j["nodes"].push_back(node);
        }
    }

    // Aggregate data (backward compatible)
    j["aggregate"] = nlohmann::json::object();
    j["aggregate"]["time_interval"] = j["metadata"]["total_time_seconds"];
    j["aggregate"]["counters"] = nlohmann::json::object();

    std::map<long, std::vector<uint64_t>> initial_counters;
    std::map<long, std::vector<uint64_t>> final_counters;
    for (size_t i = 0; i < agg.procids.size(); ++i) {
        initial_counters[agg.procids[i]] = agg.all_initial[i];
        final_counters[agg.procids[i]] = agg.all_final[i];
    }

    for (size_t i = 0; i < counter_names.size(); ++i) {
        CounterStats stats = get_counter_stats(initial_counters, final_counters, i, j["metadata"]["total_time_seconds"]);
        if (stats.sum > 0) {
            double min_sec = stats.time > 0 ? static_cast<double>(stats.min) / stats.time : 0.0;
            double mean_sec = stats.time > 0 ? stats.mean / stats.time : 0.0;
            double max_sec = stats.time > 0 ? static_cast<double>(stats.max) / stats.time : 0.0;
            j["aggregate"]["counters"][counter_names[i]] = {
                {"min", stats.min},
                {"min_per_sec", min_sec},
                {"mean", stats.mean},
                {"mean_per_sec", mean_sec},
                {"max", stats.max},
                {"max_per_sec", max_sec},
                {"sum", stats.sum}
            };
        }
    }

    std::cout << j.dump(4) << std::endl;
}

// Helper function to align timestamp to sampling interval boundary
static double align_timestamp(double timestamp) {
    double alignment = get_sample_interval() / 1000.0;  // Convert ms to seconds
    if (alignment <= 0.0) alignment = 0.1;  // Fallback to 100ms
    return std::round(timestamp / alignment) * alignment;
}

// Helper function to get output format from environment variable
static OutputFormat get_output_format() {
    const char* format_env = std::getenv("GATHER_CXI_OUTPUT_FORMAT");
    if (format_env == nullptr) {
        // Check legacy GATHER_CXI_JSON flag
        const char* json_env = std::getenv("GATHER_CXI_JSON");
        const char* detailed_env = std::getenv("GATHER_CXI_DETAILED");
        if (json_env != nullptr && std::string(json_env) == "1") {
            if (detailed_env != nullptr && std::string(detailed_env) == "1") {
                return OutputFormat::JSON;
            }
            return OutputFormat::JSON;
        }
        return OutputFormat::TEXT;  // Default
    }

    std::string format_str(format_env);
    if (format_str == "csv" || format_str == "CSV" || format_str == "csv_wide" || format_str == "CSV_WIDE") {
        return OutputFormat::CSV_WIDE;  // Default CSV is wide format
    } else if (format_str == "csv_tall" || format_str == "CSV_TALL") {
        return OutputFormat::CSV_TALL;
    } else if (format_str == "json" || format_str == "JSON") {
        return OutputFormat::JSON;
    } else {
        return OutputFormat::TEXT;
    }
}

// Helper function to get git hash of a directory
static std::string get_git_hash(const std::string& directory) {
    std::string cmd = "cd " + directory + " && git rev-parse HEAD 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "unknown";

    char buffer[128];
    std::string result;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
        // Remove newline
        if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
    }
    pclose(pipe);
    return result.empty() ? "unknown" : result;
}

// Helper function to generate markdown metadata file
static void generate_markdown_metadata(const std::string& filename, const AggregatedStats& agg,
                                       const std::vector<std::string>& cmd_args,
                                       const std::string& csv_filename,
                                       size_t row_count,
                                       bool is_wide) {
    std::ofstream md_file(filename);
    if (!md_file.is_open()) {
        std::cerr << "Warning: Could not create metadata file: " << filename << std::endl;
        return;
    }

    // Get current time
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&now_c), "%Y-%m-%d %H:%M:%S");
    std::string current_time = ss.str() + "+00:00";

    // Compute total time
    double total_time = std::accumulate(agg.times.begin(), agg.times.end(), 0.0) / (agg.times.empty() ? 1 : agg.times.size());

    // Get git hashes
    std::string gather_cxi_hash = get_git_hash("gather_cxi_counters");
    std::string rccl_hash = get_git_hash("rccl");
    std::string rccl_tests_hash = get_git_hash("rccl-tests");
    std::string aws_ofi_nccl_hash = get_git_hash("aws-ofi-nccl");

    // Write preamble
    md_file << "Experiment completed at " << current_time << " (total experiment time: "
            << std::fixed << std::setprecision(1) << total_time << "s, total rows: " << row_count << ").\n\n";

    md_file << "This file describes the conditions for the runs captured in `" << csv_filename
            << "`. The measurements were run on " << (agg.hostnames.empty() ? "unknown" : agg.hostnames[0])
            << ", starting at " << current_time << " (UTC).\n\n";

    // Git hashes
    md_file << "## Source code versions\n\n";
    md_file << "- `gather_cxi_counters` git hash: `" << gather_cxi_hash << "`\n";
    md_file << "- `rccl` git hash: `" << rccl_hash << "`\n";
    md_file << "- `rccl-tests` git hash: `" << rccl_tests_hash << "`\n";
    md_file << "- `aws-ofi-nccl` git hash: `" << aws_ofi_nccl_hash << "`\n\n";

    // Runtime options as JSON
    nlohmann::json runtime_options;
    runtime_options["command"] = cmd_args;
    runtime_options["node_count"] = agg.hostnames.size();
    runtime_options["sample_interval_ms"] = get_sample_interval();
    runtime_options["output_format"] = "csv";
    runtime_options["detailed_mode"] = is_detailed_enabled();

    md_file << "## Initial runtime options\n\n```json\n" << runtime_options.dump(2) << "\n```\n\n";

    // Environment variables that affect the run
    md_file << "## Environment variables\n\n";
    md_file << "Environment variables that may have affected the run:\n\n";
    md_file << "| Variable | Value |\n";
    md_file << "|----------|-------|\n";

    // List of relevant environment variable prefixes/names
    std::vector<std::string> env_prefixes = {
        "GATHER_CXI_", "FI_", "NCCL_", "HSA_", "HIP_", "ROCR_",
        "LD_LIBRARY_PATH", "PATH"
    };

    // Collect all environment variables
    extern char** environ;
    std::vector<std::pair<std::string, std::string>> env_vars;
    for (char** env = environ; *env != nullptr; ++env) {
        std::string entry(*env);
        size_t eq_pos = entry.find('=');
        if (eq_pos != std::string::npos) {
            std::string name = entry.substr(0, eq_pos);
            std::string value = entry.substr(eq_pos + 1);

            // Check if this variable matches any of our prefixes
            for (const auto& prefix : env_prefixes) {
                if (name.rfind(prefix, 0) == 0 || name == prefix) {
                    env_vars.push_back({name, value});
                    break;
                }
            }
        }
    }

    // Sort and output
    std::sort(env_vars.begin(), env_vars.end());
    for (const auto& [name, value] : env_vars) {
        // Escape pipe characters in values for markdown table
        std::string escaped_value = value;
        size_t pos = 0;
        while ((pos = escaped_value.find('|', pos)) != std::string::npos) {
            escaped_value.replace(pos, 1, "\\|");
            pos += 2;
        }
        // Truncate very long values (like PATH)
        if (escaped_value.length() > 100) {
            escaped_value = escaped_value.substr(0, 97) + "...";
        }
        md_file << "| " << name << " | `" << escaped_value << "` |\n";
    }
    md_file << "\n";

    // Detect which optional metric groups are present
    bool has_perf_desc = false;
    for (const auto& nts : agg.time_series) {
        for (const auto& sample : nts.samples) {
            auto it = sample.sources.find("perf");
            if (it != sample.sources.end() && !it->second.empty()) { has_perf_desc = true; break; }
        }
        if (has_perf_desc) break;
    }
    bool has_nccl_desc = false;
    for (const auto& nts : agg.time_series) {
        for (const auto& sample : nts.samples) {
            auto it = sample.sources.find("nccl");
            if (it != sample.sources.end() && !it->second.empty()) { has_nccl_desc = true; break; }
        }
        if (has_nccl_desc) break;
    }

    // CSV field descriptions (SHARP-style)
    md_file << "## CSV field description\n\n";

    if (is_wide) {
        md_file << "**Wide-format column naming**: each metric below is replicated into one "
                   "column per (node, NIC) pair and named `<metric>.<host_suffix>.<nic_suffix>`, "
                   "where `<host_suffix>` is the numeric part of the hostname (e.g. `004` for "
                   "`node004`) and `<nic_suffix>` is the integer part of the NIC name "
                   "(e.g. `0` for `hsn0`). Example: `parbs_tarb_pi_posted_pkts.004.0`.\n"
                   "The fixed columns `timestamp`, `repeat`, and `allreduce_latency_us` appear "
                   "once per row and are not replicated.\n\n";
    }

    // ── Fixed columns ──────────────────────────────────────────────────────
    md_file << "  * `timestamp` (float): Unix timestamp in seconds at sample collection, "
               "rounded to 3 decimal places.\n";
    md_file << "  * `hostname` (string): Hostname of the node that collected this sample.\n";
    md_file << "  * `nic_name` (string): High-speed network interface name (e.g. `hsn0`, `hsn1`).\n";
    md_file << "  * `repeat` (int): Allreduce cycle number, 1-indexed from the benchmark.\n";
    md_file << "  * `allreduce_latency_us` (float): End-to-end allreduce latency in microseconds, "
               "averaged over all completions that fall in this time bucket "
               "(empty when no benchmark result falls in this bucket).\n";
    md_file << "  * `max_allreduce_latency_us` (float): Maximum allreduce latency in microseconds "
               "across all completions that fall in this time bucket. "
               "Equals `allreduce_latency_us` when the bucket contains exactly one completion. "
               "In non-aligned mode, both columns are identical (one completion per row).\n";

    // ── CXI hardware counters ──────────────────────────────────────────────
    md_file << "  * `hni_rx_paused_0` (int): Times RX was flow-controlled on PCP 0 "
               "(back-pressure from peer).\n";
    md_file << "  * `hni_rx_paused_1` (int): Times RX was flow-controlled on PCP 1.\n";
    md_file << "  * `hni_tx_paused_0` (int): Times TX was flow-controlled on PCP 0 "
               "(NIC egress congestion).\n";
    md_file << "  * `hni_tx_paused_1` (int): Times TX was flow-controlled on PCP 1.\n";
    md_file << "  * `pct_no_tct_nacks` (int): NACKs due to no available TCT (target connection "
               "table) entry; indicates resource exhaustion at the target NIC.\n";
    md_file << "  * `pct_trs_rsp_nack_drops` (int): TRS response NACK drops (response discarded "
               "after NACK was sent).\n";
    md_file << "  * `parbs_tarb_pi_posted_pkts` (int): Posted (data) packets dispatched through "
               "the TX arbitration block; delta per time bucket.\n";
    md_file << "  * `parbs_tarb_pi_posted_blocked_cnt` (int): Stall cycles where posted-packet "
               "transmission was blocked by flow control.\n";
    md_file << "  * `parbs_tarb_pi_non_posted_blocked_cnt` (int): Stall cycles where non-posted "
               "(control/read-request) transmission was blocked.\n";
    md_file << "  * `parbs_tarb_pi_non_posted_pkts` (int): Non-posted packets dispatched; "
               "includes RDMA read requests and acknowledgements.\n";
    md_file << "  * `rh:connections_cancelled` (int): Request-handler: connections cancelled.\n";
    md_file << "  * `rh:nack_no_matching_conn` (int): NACKs because the incoming packet matched "
               "no open connection.\n";
    md_file << "  * `rh:nack_no_target_conn` (int): NACKs because no target connection slot was "
               "available.\n";
    md_file << "  * `rh:nack_no_target_mst` (int): NACKs due to no target MST "
               "(message-sent tracking) entry.\n";
    md_file << "  * `rh:nack_no_target_trs` (int): NACKs due to no target TRS "
               "(transport-response slot) entry.\n";
    md_file << "  * `rh:nack_resource_busy` (int): NACKs due to a resource being transiently "
               "busy.\n";
    md_file << "  * `rh:nacks` (int): Total NACKs sent by this NIC's request handler.\n";
    md_file << "  * `rh:nack_sequence_error` (int): NACKs due to a sequence-number mismatch.\n";
    md_file << "  * `rh:pkts_cancelled_o` (int): Packets cancelled due to overflow.\n";
    md_file << "  * `rh:pkts_cancelled_u` (int): Packets cancelled due to underflow.\n";
    md_file << "  * `rh:sct_in_use` (int): SCT (source connection table) entries currently in "
               "use; snapshot value, not a delta.\n";
    md_file << "  * `rh:sct_timeouts` (int): SCT entries that timed out.\n";
    md_file << "  * `rh:spt_timeouts` (int): SPT (send-packet tracking) timeout events.\n";
    md_file << "  * `rh:spt_timeouts_u` (int): SPT timeouts on unreliable transport.\n";
    md_file << "  * `rh:tct_timeouts` (int): TCT (target connection table) timeout events.\n";

    // ── Derived ratio columns ──────────────────────────────────────────────
    md_file << "  * `posted_blocked_ratio` (float): Fraction of posted-packet dispatch cycles "
               "that were stalled; `parbs_tarb_pi_posted_blocked_cnt / "
               "parbs_tarb_pi_posted_pkts`. High values indicate egress congestion.\n";
    md_file << "  * `nonposted_blocked_ratio` (float): Fraction of non-posted-packet dispatch "
               "cycles that were stalled; `parbs_tarb_pi_non_posted_blocked_cnt / "
               "parbs_tarb_pi_non_posted_pkts`.\n";

    // ── Perf/OS columns (conditional) ─────────────────────────────────────
    if (has_perf_desc) {
        md_file << "  * `cache_misses` (int): CPU last-level cache misses (hardware counter, "
                   "delta per time bucket).\n";
        md_file << "  * `context_switches` (int): OS context switches on this node "
                   "(software counter, delta).\n";
        md_file << "  * `branch_misses` (int): Branch mispredictions (hardware counter, "
                   "delta).\n";
        md_file << "  * `cpu_migrations` (int): Times a thread was migrated between CPU cores "
                   "(software counter, delta).\n";
        md_file << "  * `page_faults` (int): Page faults (software counter, delta).\n";
        md_file << "  * `dTLB_load_misses` (int): Data TLB load misses (cache hardware counter, "
                   "delta).\n";
        md_file << "  * `iTLB_load_misses` (int): Instruction TLB load misses (cache hardware "
                   "counter, delta).\n";
        md_file << "  * `emulation_faults` (int): Emulation faults (software counter, delta).\n";
        md_file << "  * `L1_icache_load_misses` (int): L1 instruction-cache load misses "
                   "(hardware counter, delta).\n";
        md_file << "  * `L1_dcache_load_misses` (int): L1 data-cache load misses (hardware "
                   "counter, delta).\n";
        md_file << "  * `cpu_clock` (int): CPU clock in nanoseconds (software counter, delta); "
                   "used with `cycles` to derive `clock_rate_GHz`.\n";
        md_file << "  * `cycles` (int): CPU cycles elapsed (hardware counter, delta).\n";
        md_file << "  * `LLC-load-misses` (int): Last-level-cache load misses; empty (NA) on "
                   "most AMD/HPE hardware.\n";
        md_file << "  * `node-load-misses` (int): NUMA node load misses; empty (NA) on most "
                   "hardware.\n";
        md_file << "  * `clock_rate_GHz` (float): Effective CPU clock rate derived as "
                   "`cycles / (cpu_clock / 1000)`.\n";
    }

    // ── NCCL/RCCL proxy-step columns (conditional) ────────────────────────
    if (has_nccl_desc) {
        md_file << "  * `nccl_send_bytes` (int): Bytes sent via the RCCL proxy thread to this "
                   "NIC in this time bucket (delta). Only proxy-path traffic is counted; "
                   "RDMA-direct transfers on `hsn1` are invisible to this counter.\n";
        md_file << "  * `nccl_send_count` (int): Number of RCCL proxy send operations completed "
                   "on this NIC in this time bucket.\n";
        md_file << "  * `nccl_send_avg_dur_us` (float): Average point-to-point send latency in "
                   "microseconds; `sum(send_duration) / nccl_send_count`. Empty when "
                   "`nccl_send_count` is zero.\n";
        md_file << "  * `nccl_recv_bytes` (int): Bytes received via the RCCL proxy thread from "
                   "this NIC in this time bucket (delta).\n";
        md_file << "  * `nccl_recv_count` (int): Number of RCCL proxy receive operations "
                   "completed on this NIC in this time bucket.\n";
        md_file << "  * `nccl_recv_avg_dur_us` (float): Average point-to-point receive latency "
                   "in microseconds; `sum(recv_duration) / nccl_recv_count`. Empty when "
                   "`nccl_recv_count` is zero.\n";
    }

    md_file << "\n";

    // Aggregate metrics
    md_file << "## Aggregate metrics\n\n";

    std::map<long, std::vector<uint64_t>> initial_counters;
    std::map<long, std::vector<uint64_t>> final_counters;
    for (size_t i = 0; i < agg.procids.size(); ++i) {
        initial_counters[agg.procids[i]] = agg.all_initial[i];
        final_counters[agg.procids[i]] = agg.all_final[i];
    }

    md_file << "| Counter | Min | Max | Mean | Sum |\n";
    md_file << "|---------|-----|-----|------|-----|\n";

    for (size_t i = 0; i < counter_names.size(); ++i) {
        CounterStats stats = get_counter_stats(initial_counters, final_counters, i, total_time);
        if (stats.sum > 0) {
            md_file << "| " << counter_names[i] << " | " << stats.min << " | " << stats.max
                    << " | " << static_cast<uint64_t>(stats.mean) << " | " << stats.sum << " |\n";
        }
    }

    md_file << "\n";

    // NEW: Document perf metrics if OS metrics were collected
    bool has_perf_metrics = false;
    for (const auto& nts : agg.time_series) {
        for (const auto& sample : nts.samples) {
            auto perf_it = sample.sources.find("perf");
            if (perf_it != sample.sources.end() && !perf_it->second.empty()) {
                has_perf_metrics = true;
                break;
            }
        }
        if (has_perf_metrics) break;
    }

    if (has_perf_metrics) {
        md_file << "## OS/CPU Performance Metrics\n\n";
        md_file << "Perf metrics were collected using `perf_event_open`. "
                   "See **CSV field description** above for per-column definitions. "
                   "Metrics showing empty values (NA) are unsupported on this hardware. "
                   "All values are per-host deltas within each 20 ms time bucket.\n\n";
    }


    md_file.close();
}

// Print CSV format: tall format with timestamp, hostname, nic_name, counter1, counter2, ...
static size_t print_csv_tall(const AggregatedStats& agg, const std::string& filename = "") {
    if (agg.time_series.empty()) {
        std::cerr << "Warning: No time-series data available for CSV output." << std::endl;
        return 0;
    }

    // Open file or use stdout
    std::ofstream file_stream;
    std::ostream* out = &std::cout;
    if (!filename.empty()) {
        file_stream.open(filename);
        if (!file_stream.is_open()) {
            std::cerr << "Error: Could not create CSV file: " << filename << std::endl;
            return 0;
        }
        out = &file_stream;
    }

    // Build latency buckets: group by aligned timestamp and average
    std::map<std::string, std::map<double, std::vector<double>>> node_latency_buckets;
    for (const auto& nts : agg.time_series) {
        for (const auto& lat_ts : nts.latency_timestamps) {
            double aligned_ts = align_timestamp(lat_ts.first);
            node_latency_buckets[nts.hostname][aligned_ts].push_back(lat_ts.second);
        }
    }

    // Average latencies within each bucket across ALL nodes
    // (latency is the same for all nodes; only one node typically reports it)
    std::map<double, std::vector<double>> global_latency_buckets;
    for (const auto& node_kv : node_latency_buckets) {
        for (const auto& ts_kv : node_kv.second) {
            auto& bucket = global_latency_buckets[ts_kv.first];
            bucket.insert(bucket.end(), ts_kv.second.begin(), ts_kv.second.end());
        }
    }

    std::map<double, double> global_avg_latencies;
    std::map<double, double> global_max_latencies;
    for (const auto& ts_kv : global_latency_buckets) {
        double sum = 0.0, mx = 0.0;
        for (double lat : ts_kv.second) {
            sum += lat;
            if (lat > mx) mx = lat;
        }
        global_avg_latencies[ts_kv.first] = sum / ts_kv.second.size();
        global_max_latencies[ts_kv.first] = mx;
    }

    // Build map from aligned timestamp to repeat numbers
    // Note: Repeat numbers are global (same across all nodes), so we don't key by hostname
    std::map<double, std::vector<int>> ts_repeat_map;
    for (const auto& nts : agg.time_series) {
        for (size_t i = 0; i < nts.latency_timestamps.size(); ++i) {
            double raw_ts = nts.latency_timestamps[i].first;
            double aligned_ts = align_timestamp(raw_ts);
            int repeat = (i < nts.repeat_numbers.size()) ? nts.repeat_numbers[i] : 0;
            ts_repeat_map[aligned_ts].push_back(repeat);
        }
    }

    // Check for multiple repeats in one aligned bucket and warn if found
    for (const auto& [aligned_ts, repeats] : ts_repeat_map) {
        if (repeats.size() > 1) {
            // Check if all repeats are the same
            bool all_same = true;
            for (size_t i = 1; i < repeats.size(); ++i) {
                if (repeats[i] != repeats[0]) {
                    all_same = false;
                    break;
                }
            }
            if (!all_same) {
                std::cerr << "Warning: Multiple different repeat numbers ("
                          << repeats[0];
                for (size_t i = 1; i < repeats.size(); ++i) {
                    std::cerr << ", " << repeats[i];
                }
                std::cerr << ") map to same aligned bucket (ts="
                          << std::fixed << std::setprecision(3) << aligned_ts
                          << "). Using first repeat number: " << repeats[0] << std::endl;
            }
        }
    }

    // Build aligned samples with DELTA computation: collect all samples and align timestamps
    // When multiple samples map to same aligned timestamp, accumulate their deltas
    std::map<std::tuple<double, std::string, std::string>, std::vector<uint64_t>> aligned_deltas;
    std::map<std::tuple<double, std::string, std::string>, int> aligned_repeats;

    // Track previous counter values per (hostname, nic_name) for delta computation
    std::map<std::pair<std::string, std::string>, std::vector<uint64_t>> prev_counters;
    std::map<std::pair<std::string, std::string>, double> prev_aligned_ts;

    for (const auto& nts : agg.time_series) {
        for (const auto& sample : nts.samples) {
            double aligned_ts = align_timestamp(sample.timestamp);

            // Create one AlignedSample per NIC with DELTA values
            for (const auto& nic_kv : sample.nic_counters) {
                auto key = std::make_pair(nts.hostname, nic_kv.first);
                auto aligned_key = std::make_tuple(aligned_ts, nts.hostname, nic_kv.first);

                // Compute deltas from previous sample
                auto prev_it = prev_counters.find(key);
                if (prev_it != prev_counters.end()) {
                    std::vector<uint64_t> delta_values(nic_kv.second.size());
                    for (size_t i = 0; i < nic_kv.second.size(); ++i) {
                        delta_values[i] = subtract_56_bit_integers(nic_kv.second[i], prev_it->second[i]);
                    }

                    // Check if same aligned bucket as previous sample
                    auto prev_aligned_it = prev_aligned_ts.find(key);
                    if (prev_aligned_it != prev_aligned_ts.end() && prev_aligned_it->second == aligned_ts) {
                        // Accumulate deltas in same bucket
                        auto& existing = aligned_deltas[aligned_key];
                        if (existing.empty()) {
                            existing = delta_values;
                        } else {
                            for (size_t i = 0; i < delta_values.size(); ++i) {
                                existing[i] += delta_values[i];
                            }
                        }
                    } else {
                        // New bucket - store delta
                        aligned_deltas[aligned_key] = delta_values;
                    }
                } else {
                    // First sample - output zeros
                    aligned_deltas[aligned_key] = std::vector<uint64_t>(nic_kv.second.size(), 0);
                }

                // Update previous counters for next iteration
                prev_counters[key] = nic_kv.second;
                prev_aligned_ts[key] = aligned_ts;
            }
        }
    }

    // Assign repeat numbers to aligned samples
    for (const auto& [key, delta_values] : aligned_deltas) {
        double aligned_ts = std::get<0>(key);
        int repeat = 0;  // Default to 0 before any latency measurements

        auto repeat_it = ts_repeat_map.find(aligned_ts);
        if (repeat_it != ts_repeat_map.end() && !repeat_it->second.empty()) {
            repeat = repeat_it->second[0];  // Use first repeat number if multiple
        }

        aligned_repeats[key] = repeat;
    }

    // Build final AlignedSample vector from accumulated deltas
    std::vector<AlignedSample> all_aligned_samples;
    for (const auto& [key, delta_values] : aligned_deltas) {
        double aligned_ts = std::get<0>(key);
        const std::string& hostname = std::get<1>(key);
        const std::string& nic_name = std::get<2>(key);

        // Find bucketed latency for this timestamp (shared across nodes)
        double bucket_latency = std::numeric_limits<double>::quiet_NaN();
        double bucket_max     = std::numeric_limits<double>::quiet_NaN();
        auto ts_it = global_avg_latencies.find(aligned_ts);
        if (ts_it != global_avg_latencies.end()) {
            bucket_latency = ts_it->second;
            bucket_max     = global_max_latencies[aligned_ts];
        }

        AlignedSample aligned;
        aligned.timestamp = aligned_ts;
        aligned.repeat = aligned_repeats[key];
        aligned.hostname = hostname;
        aligned.nic_name = nic_name;
        aligned.allreduce_latency_us = bucket_latency;
        aligned.max_allreduce_latency_us = bucket_max;
        aligned.counter_values = delta_values;
        all_aligned_samples.push_back(aligned);
    }

    // Sort by timestamp, then hostname, then nic_name
    std::sort(all_aligned_samples.begin(), all_aligned_samples.end(),
        [](const AlignedSample& a, const AlignedSample& b) {
            if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
            if (a.hostname != b.hostname) return a.hostname < b.hostname;
            return a.nic_name < b.nic_name;
        });

    // Filter to only output rows with latency data (for SHARP compatibility)
    // This prevents massive warmup sections from exceeding 100K line limits
    std::vector<AlignedSample> filtered_samples;
    if (!global_avg_latencies.empty()) {
        // Only output rows where latency data exists
        for (const auto& sample : all_aligned_samples) {
            if (global_avg_latencies.find(sample.timestamp) != global_avg_latencies.end()) {
                filtered_samples.push_back(sample);
            }
        }
    } else {
        // Fallback: output all samples if no latencies found
        filtered_samples = all_aligned_samples;
    }

    // Latency-only fallback: when GATHER_CXI_DETAILED=0 (no NIC counter samples),
    // filtered_samples will be empty even though latency data exists.
    // Generate one row per latency measurement so the CSV is still useful.
    if (filtered_samples.empty() && !global_avg_latencies.empty()) {
        for (const auto& [ts, avg_lat] : global_avg_latencies) {
            AlignedSample s;
            s.timestamp = ts;
            s.hostname = "";   // no per-host breakdown without NIC samples
            s.nic_name = "";
            {
                auto it = ts_repeat_map.find(ts);
                s.repeat = (it != ts_repeat_map.end() && !it->second.empty())
                               ? it->second[0] : 0;
            }
            s.allreduce_latency_us     = avg_lat;
            s.max_allreduce_latency_us = global_max_latencies.count(ts)
                                             ? global_max_latencies.at(ts) : avg_lat;
            // counter_values intentionally left empty — no NIC data collected
            filtered_samples.push_back(s);
        }
    }


    // Perf metrics are per-host (not per-NIC), so we'll replicate values across all NICs of a host
    std::vector<std::string> perf_metric_names;
    std::map<std::tuple<double, std::string>, std::map<std::string, uint64_t>> perf_deltas;
    std::map<std::string, std::map<std::string, uint64_t>> prev_perf_values;
    std::map<std::string, double> prev_perf_aligned_ts;

    // First pass: collect perf metric names from any sample
    for (const auto& nts : agg.time_series) {
        for (const auto& sample : nts.samples) {
            auto perf_it = sample.sources.find("perf");
            if (perf_it != sample.sources.end()) {
                for (const auto& [entity_id, metrics] : perf_it->second) {
                    for (const auto& [metric_name, value] : metrics) {
                        if (std::find(perf_metric_names.begin(), perf_metric_names.end(), metric_name) == perf_metric_names.end()) {
                            perf_metric_names.push_back(metric_name);
                        }
                    }
                }
                break;
            }
        }
        if (!perf_metric_names.empty()) break;
    }

    // Sort perf metric names for consistent ordering (cache_misses, context_switches, etc.)
    // Keep order: cache_misses, context_switches, branch_misses, cpu_migrations, page_faults,
    // dTLB_load_misses, iTLB_load_misses, emulation_faults, L1_icache_load_misses,
    // L1_dcache_load_misses, cpu_clock, cycles, LLC-load-misses, node-load-misses
    std::vector<std::string> expected_order = {
        "cache_misses", "context_switches", "branch_misses", "cpu_migrations", "page_faults",
        "dTLB_load_misses", "iTLB_load_misses", "emulation_faults", "L1_icache_load_misses",
        "L1_dcache_load_misses", "cpu_clock", "cycles", "LLC-load-misses", "node-load-misses"
    };
    std::vector<std::string> sorted_perf_names;
    for (const auto& name : expected_order) {
        if (std::find(perf_metric_names.begin(), perf_metric_names.end(), name) != perf_metric_names.end()) {
            sorted_perf_names.push_back(name);
        }
    }
    // Add any unexpected metrics at the end
    for (const auto& name : perf_metric_names) {
        if (std::find(sorted_perf_names.begin(), sorted_perf_names.end(), name) == sorted_perf_names.end()) {
            sorted_perf_names.push_back(name);
        }
    }
    perf_metric_names = sorted_perf_names;

    // Second pass: compute deltas for perf metrics
    for (const auto& nts : agg.time_series) {
        for (const auto& sample : nts.samples) {
            double aligned_ts = align_timestamp(sample.timestamp);

            auto perf_it = sample.sources.find("perf");
            if (perf_it != sample.sources.end()) {
                for (const auto& [entity_id, metrics] : perf_it->second) {
                    // entity_id is hostname for perf metrics
                    auto prev_it = prev_perf_values.find(entity_id);
                    if (prev_it != prev_perf_values.end()) {
                        // Compute deltas
                        std::map<std::string, uint64_t> delta_metrics;
                        for (const auto& [metric_name, value] : metrics) {
                            auto prev_metric_it = prev_it->second.find(metric_name);
                            if (prev_metric_it != prev_it->second.end()) {
                                // Handle UINT64_MAX (unsupported events)
                                if (value == UINT64_MAX || prev_metric_it->second == UINT64_MAX) {
                                    delta_metrics[metric_name] = UINT64_MAX;
                                } else {
                                    delta_metrics[metric_name] = value - prev_metric_it->second;
                                }
                            } else {
                                delta_metrics[metric_name] = 0;  // First occurrence of this metric
                            }
                        }

                        // Check if same aligned bucket as previous sample
                        auto prev_aligned_it = prev_perf_aligned_ts.find(entity_id);
                        if (prev_aligned_it != prev_perf_aligned_ts.end() && prev_aligned_it->second == aligned_ts) {
                            // Same bucket - accumulate deltas
                            auto key = std::make_tuple(aligned_ts, entity_id);
                            auto& existing = perf_deltas[key];
                            if (existing.empty()) {
                                existing = delta_metrics;
                            } else {
                                for (const auto& [metric_name, delta_value] : delta_metrics) {
                                    if (delta_value == UINT64_MAX || existing[metric_name] == UINT64_MAX) {
                                        existing[metric_name] = UINT64_MAX;
                                    } else {
                                        existing[metric_name] += delta_value;
                                    }
                                }
                            }
                        } else {
                            // New bucket - store delta
                            perf_deltas[std::make_tuple(aligned_ts, entity_id)] = delta_metrics;
                        }
                    } else {
                        // First sample - output zeros
                        std::map<std::string, uint64_t> zero_metrics;
                        for (const auto& [metric_name, value] : metrics) {
                            zero_metrics[metric_name] = 0;
                        }
                        perf_deltas[std::make_tuple(aligned_ts, entity_id)] = zero_metrics;
                    }

                    // Update previous values for next iteration
                    prev_perf_values[entity_id] = metrics;
                    prev_perf_aligned_ts[entity_id] = aligned_ts;
                }
            }
        }
    }

    // Collect NCCL metrics and compute deltas.
    // Entity keys in sources["nccl"] are NIC names (e.g. "hsn0") from nic_for_channel().
    // Keyed by (aligned_ts, hostname, nic_name) so each row looks up directly.
    std::map<std::tuple<double, std::string, std::string>, std::map<std::string, uint64_t>> nccl_deltas;
    {
        std::map<std::pair<std::string, std::string>, std::map<std::string, uint64_t>> prev_vals;
        std::map<std::pair<std::string, std::string>, double> prev_ts;
        for (const auto& nts : agg.time_series) {
            for (const auto& sample : nts.samples) {
                double aligned_ts = align_timestamp(sample.timestamp);
                auto it = sample.sources.find("nccl");
                if (it == sample.sources.end()) continue;
                for (const auto& [nic_name, metrics] : it->second) {
                    auto host_nic = std::make_pair(nts.hostname, nic_name);
                    auto prev_it  = prev_vals.find(host_nic);
                    std::map<std::string, uint64_t> delta;
                    if (prev_it != prev_vals.end()) {
                        for (const auto& [mn, v] : metrics) {
                            auto pm = prev_it->second.find(mn);
                            delta[mn] = (pm != prev_it->second.end()) ? v - pm->second : 0;
                        }
                        auto nccl_key = std::make_tuple(aligned_ts, nts.hostname, nic_name);
                        auto pts = prev_ts.find(host_nic);
                        if (pts != prev_ts.end() && pts->second == aligned_ts) {
                            auto& ex = nccl_deltas[nccl_key];
                            if (ex.empty()) ex = delta;
                            else for (const auto& [mn, dv] : delta) ex[mn] += dv;
                        } else {
                            nccl_deltas[nccl_key] = delta;
                        }
                    } else {
                        for (const auto& [mn, v] : metrics) delta[mn] = 0;
                        nccl_deltas[std::make_tuple(aligned_ts, nts.hostname, nic_name)] = delta;
                    }
                    prev_vals[host_nic] = metrics;
                    prev_ts[host_nic]   = aligned_ts;
                }
            }
        }
    }
    const bool has_nccl = !nccl_deltas.empty();

    // Print CSV header
    *out << "timestamp,hostname,nic_name,repeat,allreduce_latency_us,max_allreduce_latency_us";
    for (const auto& counter_name : counter_names) {
        *out << "," << counter_name;
    }
    *out << ",posted_blocked_ratio,nonposted_blocked_ratio";
    // Add perf metric columns
    for (const auto& perf_name : perf_metric_names) {
        *out << "," << perf_name;
    }
    // Add derived metric column (clock_rate)
    if (!perf_metric_names.empty()) {
        *out << ",clock_rate_GHz";
    }
    // Add NCCL columns (send/recv bytes, count, avg p2p latency per NIC)
    if (has_nccl) {
        *out << ",nccl_send_bytes,nccl_send_count,nccl_send_avg_dur_us"
                ",nccl_recv_bytes,nccl_recv_count,nccl_recv_avg_dur_us";
    }
    *out << std::endl;

    // Print CSV rows (only those with latency data)
    for (const auto& sample : filtered_samples) {
        *out << std::fixed << std::setprecision(3) << sample.timestamp << ","
                  << sample.hostname << ","
                  << sample.nic_name << ","
                  << sample.repeat << ",";
        if (std::isnan(sample.allreduce_latency_us)) {
            *out << ",";
        } else {
            *out << std::setprecision(2) << sample.allreduce_latency_us << ",";
            if (!std::isnan(sample.max_allreduce_latency_us))
                *out << std::setprecision(2) << sample.max_allreduce_latency_us;
        }
        for (const auto& val : sample.counter_values) {
            *out << "," << val;
        }

        // Compute and output blocked ratios
        // Index 6: parbs_tarb_pi_posted_pkts
        // Index 7: parbs_tarb_pi_posted_blocked_cnt
        // Index 8: parbs_tarb_pi_non_posted_blocked_cnt
        // Index 9: parbs_tarb_pi_non_posted_pkts
        if (sample.counter_values.size() >= 10) {
            uint64_t posted_pkts = sample.counter_values[6];
            uint64_t posted_blocked = sample.counter_values[7];
            uint64_t nonposted_blocked = sample.counter_values[8];
            uint64_t nonposted_pkts = sample.counter_values[9];

            // Compute posted_blocked_ratio
            if (posted_pkts > 0) {
                *out << "," << std::fixed << std::setprecision(6)
                     << (static_cast<double>(posted_blocked) / static_cast<double>(posted_pkts));
            } else {
                *out << ",";  // Empty for NA
            }

            // Compute nonposted_blocked_ratio
            if (nonposted_pkts > 0) {
                *out << "," << std::fixed << std::setprecision(6)
                     << (static_cast<double>(nonposted_blocked) / static_cast<double>(nonposted_pkts));
            } else {
                *out << ",";  // Empty for NA
            }
        } else {
            *out << ",,";  // Both empty if not enough counters
        }

        // Output perf metric values (same for all NICs of this host)
        auto perf_key = std::make_tuple(sample.timestamp, sample.hostname);
        auto perf_it = perf_deltas.find(perf_key);
        uint64_t cycles_val = UINT64_MAX;
        uint64_t cpu_clock_val = UINT64_MAX;
        for (const auto& perf_name : perf_metric_names) {
            *out << ",";
            if (perf_it != perf_deltas.end()) {
                auto metric_it = perf_it->second.find(perf_name);
                if (metric_it != perf_it->second.end()) {
                    if (metric_it->second == UINT64_MAX) {
                        // Unsupported event - output empty for NA
                        *out << "";
                    } else {
                        *out << metric_it->second;
                        // Track cycles and cpu_clock for clock_rate calculation
                        if (perf_name == "cycles") cycles_val = metric_it->second;
                        if (perf_name == "cpu_clock") cpu_clock_val = metric_it->second;
                    }
                }
            }
            // else: empty (no perf data for this metric)
        }
        
        // Output derived metric: clock_rate_GHz = cycles / (cpu_clock / 1000)
        if (!perf_metric_names.empty()) {
            *out << ",";
            if (cycles_val != UINT64_MAX && cpu_clock_val != UINT64_MAX && cpu_clock_val > 0) {
                double clock_rate_ghz = static_cast<double>(cycles_val) / (static_cast<double>(cpu_clock_val) / 1000.0);
                *out << std::fixed << std::setprecision(2) << clock_rate_ghz;
            }
            // else: empty (cannot compute clock_rate)
        }

        // Output NCCL per-NIC values (6 columns, empty when NIC has no proxy traffic)
        if (has_nccl) {
            auto nk = std::make_tuple(sample.timestamp, sample.hostname, sample.nic_name);
            auto ni = nccl_deltas.find(nk);
            if (ni != nccl_deltas.end()) {
                const auto& m = ni->second;
                auto get = [&](const char* name) -> uint64_t {
                    auto it = m.find(name); return it != m.end() ? it->second : 0;
                };
                uint64_t sc = get("nccl_send_count"),  sb = get("nccl_send_bytes"),
                         sd = get("nccl_send_duration_us");
                uint64_t rc = get("nccl_recv_count"),  rb = get("nccl_recv_bytes"),
                         rd = get("nccl_recv_duration_us");
                *out << "," << sb << "," << sc << ",";
                if (sc > 0) *out << std::fixed << std::setprecision(1) << (double)sd / sc;
                *out << "," << rb << "," << rc << ",";
                if (rc > 0) *out << std::fixed << std::setprecision(1) << (double)rd / rc;
            } else {
                *out << ",,,,,,";
            }
        }

        *out << std::endl;
    }

    if (file_stream.is_open()) {
        file_stream.close();
    }

    return filtered_samples.size();
}

// Time-series sampling thread function
void sampling_thread(std::atomic<bool>& running, NodeTimeSeries& node_ts, int interval_ms) {
    MetricRegistry& registry = MetricRegistry::instance();
    
    while (running.load()) {
        // Get current UNIX timestamp in seconds with millisecond precision (3 decimal places)
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        double timestamp = std::chrono::duration<double>(duration).count();
        // Round to 3 decimal places
        timestamp = std::round(timestamp * 1000.0) / 1000.0;

        // Create time sample
        TimeSample sample;
        sample.timestamp = timestamp;

        // Use registry to collect metrics from all sources
        for (const auto& source_name : registry.source_names()) {
            auto* source = registry.lookup(source_name);
            if (!source) {
                continue;
            }
            try {
                auto src_sample = source->read();
                for (const auto& [entity_id, metrics] : src_sample.entities) {
                    sample.sources[source_name][entity_id] = metrics;
                }
            } catch (...) {
                // Keep backward compat data if a source fails
            }
        }

        // Populate legacy nic_counters from registry "nic" source data
        // so that all existing JSON/CSV formatters continue to work.
        auto nic_it = sample.sources.find("nic");
        if (nic_it != sample.sources.end()) {
            for (const auto& [nic_name, metrics] : nic_it->second) {
                std::vector<uint64_t> vals(counter_names.size(), 0);
                for (size_t i = 0; i < counter_names.size(); ++i) {
                    auto m = metrics.find(counter_names[i]);
                    if (m != metrics.end()) vals[i] = m->second;
                }
                sample.nic_counters[nic_name] = vals;
            }
        }

        node_ts.samples.push_back(sample);

        // Sleep for the specified interval
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

// Extract numeric suffix from hostname (e.g., "bardpeak001" -> "001")
static std::string extract_host_number(const std::string& hostname) {
    size_t i = hostname.length();
    while (i > 0 && isdigit(hostname[i-1])) {
        i--;
    }
    if (i < hostname.length()) {
        return hostname.substr(i);
    }
    return hostname;  // Fallback to full hostname
}

// Extract numeric suffix from NIC name (e.g., "hsn0" -> "0")
static std::string extract_nic_number(const std::string& nic_name) {
    size_t i = nic_name.length();
    while (i > 0 && isdigit(nic_name[i-1])) {
        i--;
    }
    if (i < nic_name.length()) {
        return nic_name.substr(i);
    }
    return nic_name;  // Fallback to full nic name
}

// Print CSV format: wide format with one row per timestamp (800+ columns)
static size_t print_csv_wide(const AggregatedStats& agg, const std::string& filename = "") {
    if (agg.time_series.empty()) {
        std::cerr << "Warning: No time-series data available for CSV output." << std::endl;
        return 0;
    }

    // Open file or use stdout
    std::ofstream file_stream;
    std::ostream* out = &std::cout;
    if (!filename.empty()) {
        file_stream.open(filename);
        if (!file_stream.is_open()) {
            std::cerr << "Error: Could not create CSV file: " << filename << std::endl;
            return 0;
        }
        out = &file_stream;
    }

    const auto& counter_names = ::counter_names;

    // Build latency buckets (same as tall format)
    std::map<std::string, std::map<double, std::vector<double>>> node_latency_buckets;
    for (const auto& nts : agg.time_series) {
        for (const auto& [timestamp, latency_us] : nts.latency_timestamps) {
            double aligned_ts = align_timestamp(timestamp);
            node_latency_buckets[nts.hostname][aligned_ts].push_back(latency_us);
        }
    }

    // Compute average latency for each bucket
    std::map<std::string, std::map<double, double>> node_avg_latencies;
    std::map<std::string, std::map<double, double>> node_max_latencies;
    for (const auto& [hostname, ts_map] : node_latency_buckets) {
        for (const auto& [timestamp, latencies] : ts_map) {
            double sum = 0.0, mx = 0.0;
            for (double lat : latencies) {
                sum += lat;
                if (lat > mx) mx = lat;
            }
            node_avg_latencies[hostname][timestamp] = sum / latencies.size();
            node_max_latencies[hostname][timestamp] = mx;
        }
    }

    // Build map from aligned timestamp to repeat numbers (first repeat in bucket)
    // Note: Repeat numbers are global (same across all nodes), so we don't key by hostname
    std::map<double, int> ts_first_repeat_map;
    for (const auto& nts : agg.time_series) {
        for (size_t i = 0; i < nts.latency_timestamps.size(); ++i) {
            double raw_ts = nts.latency_timestamps[i].first;
            double aligned_ts = align_timestamp(raw_ts);
            int repeat = (i < nts.repeat_numbers.size()) ? nts.repeat_numbers[i] : 0;

            // Only set if not already set (keeps first repeat in bucket)
            if (ts_first_repeat_map.find(aligned_ts) == ts_first_repeat_map.end()) {
                ts_first_repeat_map[aligned_ts] = repeat;
            } else {
                // Check if different repeat numbers map to same bucket
                if (ts_first_repeat_map[aligned_ts] != repeat) {
                    std::cerr << "Warning: Multiple different repeat numbers ("
                              << ts_first_repeat_map[aligned_ts] << ", " << repeat
                              << ") map to same aligned bucket (ts="
                              << std::fixed << std::setprecision(3) << aligned_ts
                              << "). Using first repeat number: " << ts_first_repeat_map[aligned_ts] << std::endl;
                }
            }
        }
    }

    // Collect all unique timestamps
    std::set<double> all_timestamps;
    std::set<double> timestamps_with_latency;  // Track which timestamps have latency data

    for (const auto& nts : agg.time_series) {
        for (const auto& sample : nts.samples) {
            double aligned_ts = align_timestamp(sample.timestamp);
            all_timestamps.insert(aligned_ts);
        }
        // Track timestamps that have latency measurements
        for (const auto& lat_pair : nts.latency_timestamps) {
            double aligned_ts = align_timestamp(lat_pair.first);
            timestamps_with_latency.insert(aligned_ts);
        }
    }

    // For SHARP compatibility, only output rows with latency data
    // This prevents massive warmup sections from exceeding 100K line limits
    double first_latency_ts = 0.0;
    if (!timestamps_with_latency.empty()) {
        first_latency_ts = *timestamps_with_latency.begin();
    }

    // Filter all_timestamps to ONLY include those that have latency measurements
    // If no latencies, output all (shouldn't happen with proper benchmarks)
    std::set<double> filtered_timestamps;
    if (!timestamps_with_latency.empty()) {
        // Strict: only output timestamps with latency data
        filtered_timestamps = timestamps_with_latency;
    } else {
        // Fallback: output all timestamps if no latencies found
        filtered_timestamps = all_timestamps;
    }



    // Build map of (timestamp, hostname, nic) -> counter values
    // For DELTA computation, we need to process timestamps in order and SUM deltas within same bucket
    std::map<double, std::map<std::string, std::map<std::string, std::vector<uint64_t>>>> data_map;
    std::map<std::pair<std::string, std::string>, std::vector<uint64_t>> prev_counters;
    std::map<std::pair<std::string, std::string>, double> prev_aligned_ts;

    for (const auto& nts : agg.time_series) {
        for (const auto& sample : nts.samples) {
            double aligned_ts = align_timestamp(sample.timestamp);
            for (const auto& [nic_name, counter_values] : sample.nic_counters) {
                auto key = std::make_pair(nts.hostname, nic_name);
                std::vector<uint64_t> delta_values;

                auto prev_it = prev_counters.find(key);
                if (prev_it != prev_counters.end()) {
                    // Compute deltas from previous raw sample
                    delta_values.resize(counter_values.size());
                    for (size_t i = 0; i < counter_values.size(); ++i) {
                        delta_values[i] = subtract_56_bit_integers(counter_values[i], prev_it->second[i]);
                    }

                    // Check if this maps to same aligned bucket as previous sample
                    auto prev_aligned_it = prev_aligned_ts.find(key);
                    if (prev_aligned_it != prev_aligned_ts.end() && prev_aligned_it->second == aligned_ts) {
                        // Same bucket - accumulate deltas
                        auto& existing = data_map[aligned_ts][nts.hostname][nic_name];
                        if (existing.empty()) {
                            existing = delta_values;
                        } else {
                            for (size_t i = 0; i < delta_values.size(); ++i) {
                                existing[i] += delta_values[i];
                            }
                        }
                    } else {
                        // New bucket - store delta
                        data_map[aligned_ts][nts.hostname][nic_name] = delta_values;
                    }
                } else {
                    // First sample - output zeros
                    delta_values.resize(counter_values.size(), 0);
                    data_map[aligned_ts][nts.hostname][nic_name] = delta_values;
                }

                prev_counters[key] = counter_values;  // Store raw values for next delta
                prev_aligned_ts[key] = aligned_ts;     // Track which bucket this was in
            }
        }
    }

    // Collect all unique (hostname, nic) pairs to build column headers
    std::set<std::pair<std::string, std::string>> host_nic_pairs;
    for (const auto& nts : agg.time_series) {
        for (const auto& sample : nts.samples) {
            for (const auto& [nic_name, _] : sample.nic_counters) {
                host_nic_pairs.insert({nts.hostname, nic_name});
            }
        }
    }

    // Sort pairs by hostname then nic for consistent column ordering
    std::vector<std::pair<std::string, std::string>> sorted_pairs(host_nic_pairs.begin(), host_nic_pairs.end());
    std::sort(sorted_pairs.begin(), sorted_pairs.end());

    // NEW: Collect perf metrics from sources["perf"] and compute deltas
    std::vector<std::string> perf_metric_names;
    std::map<double, std::map<std::string, std::map<std::string, uint64_t>>> perf_data_map;
    std::map<std::string, std::map<std::string, uint64_t>> prev_perf_values;
    std::map<std::string, double> prev_perf_aligned_ts;

    // First pass: collect perf metric names from any sample
    for (const auto& nts : agg.time_series) {
        for (const auto& sample : nts.samples) {
            auto perf_it = sample.sources.find("perf");
            if (perf_it != sample.sources.end()) {
                for (const auto& [entity_id, metrics] : perf_it->second) {
                    for (const auto& [metric_name, value] : metrics) {
                        if (std::find(perf_metric_names.begin(), perf_metric_names.end(), metric_name) == perf_metric_names.end()) {
                            perf_metric_names.push_back(metric_name);
                        }
                    }
                }
                break;
            }
        }
        if (!perf_metric_names.empty()) break;
    }

    // Sort perf metric names for consistent ordering
    std::vector<std::string> expected_order = {
        "cache_misses", "context_switches", "branch_misses", "cpu_migrations", "page_faults",
        "dTLB_load_misses", "iTLB_load_misses", "emulation_faults", "L1_icache_load_misses",
        "L1_dcache_load_misses", "cpu_clock", "cycles", "LLC-load-misses", "node-load-misses"
    };
    std::vector<std::string> sorted_perf_names;
    for (const auto& name : expected_order) {
        if (std::find(perf_metric_names.begin(), perf_metric_names.end(), name) != perf_metric_names.end()) {
            sorted_perf_names.push_back(name);
        }
    }
    for (const auto& name : perf_metric_names) {
        if (std::find(sorted_perf_names.begin(), sorted_perf_names.end(), name) == sorted_perf_names.end()) {
            sorted_perf_names.push_back(name);
        }
    }
    perf_metric_names = sorted_perf_names;

    // Second pass: compute deltas for perf metrics
    for (const auto& nts : agg.time_series) {
        for (const auto& sample : nts.samples) {
            double aligned_ts = align_timestamp(sample.timestamp);

            auto perf_it = sample.sources.find("perf");
            if (perf_it != sample.sources.end()) {
                for (const auto& [entity_id, metrics] : perf_it->second) {
                    auto prev_it = prev_perf_values.find(entity_id);
                    if (prev_it != prev_perf_values.end()) {
                        std::map<std::string, uint64_t> delta_metrics;
                        for (const auto& [metric_name, value] : metrics) {
                            auto prev_metric_it = prev_it->second.find(metric_name);
                            if (prev_metric_it != prev_it->second.end()) {
                                if (value == UINT64_MAX || prev_metric_it->second == UINT64_MAX) {
                                    delta_metrics[metric_name] = UINT64_MAX;
                                } else {
                                    delta_metrics[metric_name] = value - prev_metric_it->second;
                                }
                            } else {
                                delta_metrics[metric_name] = 0;
                            }
                        }

                        auto prev_aligned_it = prev_perf_aligned_ts.find(entity_id);
                        if (prev_aligned_it != prev_perf_aligned_ts.end() && prev_aligned_it->second == aligned_ts) {
                            // Same bucket - accumulate deltas
                            auto& existing = perf_data_map[aligned_ts][entity_id];
                            if (existing.empty()) {
                                existing = delta_metrics;
                            } else {
                                for (const auto& [metric_name, delta_value] : delta_metrics) {
                                    if (delta_value == UINT64_MAX || existing[metric_name] == UINT64_MAX) {
                                        existing[metric_name] = UINT64_MAX;
                                    } else {
                                        existing[metric_name] += delta_value;
                                    }
                                }
                            }
                        } else {
                            // New bucket - store delta
                            perf_data_map[aligned_ts][entity_id] = delta_metrics;
                        }
                    } else {
                        // First sample - output zeros
                        std::map<std::string, uint64_t> zero_metrics;
                        for (const auto& [metric_name, value] : metrics) {
                            zero_metrics[metric_name] = 0;
                        }
                        perf_data_map[aligned_ts][entity_id] = zero_metrics;
                    }

                    prev_perf_values[entity_id] = metrics;
                    prev_perf_aligned_ts[entity_id] = aligned_ts;
                }
            }
        }
    }

    // Collect NCCL metrics and compute deltas (wide format).
    // Entity keys in sources["nccl"] are NIC names (e.g. "hsn0") from nic_for_channel().
    std::map<std::tuple<double, std::string, std::string>, std::map<std::string, uint64_t>> nccl_deltas;
    {
        std::map<std::pair<std::string, std::string>, std::map<std::string, uint64_t>> prev_vals;
        std::map<std::pair<std::string, std::string>, double> prev_ts;
        for (const auto& nts : agg.time_series) {
            for (const auto& sample : nts.samples) {
                double aligned_ts = align_timestamp(sample.timestamp);
                auto it = sample.sources.find("nccl");
                if (it == sample.sources.end()) continue;
                for (const auto& [nic_name, metrics] : it->second) {
                    auto host_nic = std::make_pair(nts.hostname, nic_name);
                    auto prev_it  = prev_vals.find(host_nic);
                    std::map<std::string, uint64_t> delta;
                    if (prev_it != prev_vals.end()) {
                        for (const auto& [mn, v] : metrics) {
                            auto pm = prev_it->second.find(mn);
                            delta[mn] = (pm != prev_it->second.end()) ? v - pm->second : 0;
                        }
                        auto nccl_key = std::make_tuple(aligned_ts, nts.hostname, nic_name);
                        auto pts = prev_ts.find(host_nic);
                        if (pts != prev_ts.end() && pts->second == aligned_ts) {
                            auto& ex = nccl_deltas[nccl_key];
                            if (ex.empty()) ex = delta;
                            else for (const auto& [mn, dv] : delta) ex[mn] += dv;
                        } else {
                            nccl_deltas[nccl_key] = delta;
                        }
                    } else {
                        for (const auto& [mn, v] : metrics) delta[mn] = 0;
                        nccl_deltas[std::make_tuple(aligned_ts, nts.hostname, nic_name)] = delta;
                    }
                    prev_vals[host_nic] = metrics;
                    prev_ts[host_nic]   = aligned_ts;
                }
            }
        }
    }
    const bool has_nccl = !nccl_deltas.empty();

    // Collect all unique hostnames for perf columns
    std::set<std::string> perf_hostnames;
    for (const auto& [ts, host_map] : perf_data_map) {
        for (const auto& [hostname, _] : host_map) {
            perf_hostnames.insert(hostname);
        }
    }
    std::vector<std::string> sorted_perf_hosts(perf_hostnames.begin(), perf_hostnames.end());
    std::sort(sorted_perf_hosts.begin(), sorted_perf_hosts.end());

    // Print header: timestamp, repeat, allreduce_latency_us, max_allreduce_latency_us, then counter columns
    *out << "timestamp,repeat,allreduce_latency_us,max_allreduce_latency_us";
    for (const auto& counter_name : counter_names) {
        for (const auto& [hostname, nic_name] : sorted_pairs) {
            std::string host_num = extract_host_number(hostname);
            std::string nic_num = extract_nic_number(nic_name);
            *out << "," << counter_name << "." << host_num << "." << nic_num;
        }
    }
    // Add headers for posted_blocked_ratio
    for (const auto& [hostname, nic_name] : sorted_pairs) {
        std::string host_num = extract_host_number(hostname);
        std::string nic_num = extract_nic_number(nic_name);
        *out << ",posted_blocked_ratio." << host_num << "." << nic_num;
    }
    // Add headers for nonposted_blocked_ratio
    for (const auto& [hostname, nic_name] : sorted_pairs) {
        std::string host_num = extract_host_number(hostname);
        std::string nic_num = extract_nic_number(nic_name);
        *out << ",nonposted_blocked_ratio." << host_num << "." << nic_num;
    }
    // Add headers for perf metrics
    for (const auto& perf_name : perf_metric_names) {
        for (const auto& hostname : sorted_perf_hosts) {
            std::string host_num = extract_host_number(hostname);
            *out << "," << perf_name << "." << host_num;
        }
    }
    // Add headers for derived metric: clock_rate
    if (!perf_metric_names.empty()) {
        for (const auto& hostname : sorted_perf_hosts) {
            std::string host_num = extract_host_number(hostname);
            *out << ",clock_rate_GHz." << host_num;
        }
    }
    // Add NCCL columns per (host, nic) pair
    if (has_nccl) {
        static const char* nccl_wide_cols[] = {
            "nccl_send_bytes", "nccl_send_count", "nccl_send_avg_dur_us",
            "nccl_recv_bytes", "nccl_recv_count", "nccl_recv_avg_dur_us",
        };
        for (const char* col : nccl_wide_cols) {
            for (const auto& [hostname, nic_name] : sorted_pairs) {
                std::string host_num = extract_host_number(hostname);
                std::string nic_num  = extract_nic_number(nic_name);
                *out << "," << col << "." << host_num << "." << nic_num;
            }
        }
    }
    *out << std::endl;

    // Print data rows
    for (double timestamp : filtered_timestamps) {
        *out << std::fixed << std::setprecision(3) << timestamp;

        // Find repeat number for this timestamp
        int repeat_num = 0;  // Default to 0 before any latency measurements
        auto repeat_it = ts_first_repeat_map.find(timestamp);
        if (repeat_it != ts_first_repeat_map.end()) {
            repeat_num = repeat_it->second;
        }
        *out << "," << repeat_num;

        // Find average latency for this timestamp (use first node that has data)
        // If no latency measurement exists for this bucket, output empty (NA) instead of 0
        bool found_latency = false;
        double avg_latency = 0.0, max_latency = 0.0;
        for (const auto& [hostname, ts_map] : node_avg_latencies) {
            auto it = ts_map.find(timestamp);
            if (it != ts_map.end()) {
                avg_latency = it->second;
                found_latency = true;
                break;
            }
        }
        if (found_latency) {
            *out << std::setprecision(2) << "," << avg_latency;
            // Find max latency for the same bucket
            for (const auto& [hostname, ts_map] : node_max_latencies) {
                auto it = ts_map.find(timestamp);
                if (it != ts_map.end()) { max_latency = it->second; break; }
            }
            *out << "," << max_latency;
        } else {
            *out << ",,";  // Empty = NA for both avg and max
        }

        // Print counter values for each (host, nic) pair
        for (size_t counter_idx = 0; counter_idx < counter_names.size(); counter_idx++) {
            for (const auto& [hostname, nic_name] : sorted_pairs) {
                *out << ",";
                auto ts_it = data_map.find(timestamp);
                if (ts_it != data_map.end()) {
                    auto host_it = ts_it->second.find(hostname);
                    if (host_it != ts_it->second.end()) {
                        auto nic_it = host_it->second.find(nic_name);
                        if (nic_it != host_it->second.end()) {
                            const auto& counter_values = nic_it->second;
                            if (counter_idx < counter_values.size()) {
                                *out << counter_values[counter_idx];
                            }
                            // else: empty (no value for this counter)
                        }
                        // else: empty (no data for this NIC)
                    }
                    // else: empty (no data for this host)
                }
                // else: empty (no data for this timestamp)
            }
        }

        // Output posted_blocked_ratio for each (host, nic) pair
        for (const auto& [hostname, nic_name] : sorted_pairs) {
            *out << ",";
            auto ts_it = data_map.find(timestamp);
            if (ts_it != data_map.end()) {
                auto host_it = ts_it->second.find(hostname);
                if (host_it != ts_it->second.end()) {
                    auto nic_it = host_it->second.find(nic_name);
                    if (nic_it != host_it->second.end()) {
                        const auto& counter_values = nic_it->second;
                        if (counter_values.size() >= 7) {
                            uint64_t posted_pkts = counter_values[6];
                            uint64_t posted_blocked = counter_values[7];
                            if (posted_pkts > 0) {
                                *out << std::fixed << std::setprecision(6)
                                     << (static_cast<double>(posted_blocked) / static_cast<double>(posted_pkts));
                            } else {
                                *out << "0.0";  // 0.0 when no packets (ensures numeric type)
                            }
                        } else {
                            *out << "0.0";  // 0.0 if not enough counters
                        }
                    } else {
                        *out << "0.0";  // 0.0 if no data for this NIC
                    }
                } else {
                    *out << "0.0";  // 0.0 if no data for this host
                }
            } else {
                *out << "0.0";  // 0.0 if no data for this timestamp
            }
        }

        // Output nonposted_blocked_ratio for each (host, nic) pair
        for (const auto& [hostname, nic_name] : sorted_pairs) {
            *out << ",";
            auto ts_it = data_map.find(timestamp);
            if (ts_it != data_map.end()) {
                auto host_it = ts_it->second.find(hostname);
                if (host_it != ts_it->second.end()) {
                    auto nic_it = host_it->second.find(nic_name);
                    if (nic_it != host_it->second.end()) {
                        const auto& counter_values = nic_it->second;
                        if (counter_values.size() >= 10) {
                            uint64_t nonposted_blocked = counter_values[8];
                            uint64_t nonposted_pkts = counter_values[9];
                            if (nonposted_pkts > 0) {
                                *out << std::fixed << std::setprecision(6)
                                     << (static_cast<double>(nonposted_blocked) / static_cast<double>(nonposted_pkts));
                            } else {
                                *out << "0.0";  // 0.0 when no packets (ensures numeric type)
                            }
                        } else {
                            *out << "0.0";  // 0.0 if not enough counters
                        }
                    } else {
                        *out << "0.0";  // 0.0 if no data for this NIC
                    }
                } else {
                    *out << "0.0";  // 0.0 if no data for this host
                }
            } else {
                *out << "0.0";  // 0.0 if no data for this timestamp
            }
        }

        // Output perf metric values for each host
        for (const auto& perf_name : perf_metric_names) {
            for (const auto& hostname : sorted_perf_hosts) {
                *out << ",";
                auto ts_it = perf_data_map.find(timestamp);
                if (ts_it != perf_data_map.end()) {
                    auto host_it = ts_it->second.find(hostname);
                    if (host_it != ts_it->second.end()) {
                        auto metric_it = host_it->second.find(perf_name);
                        if (metric_it != host_it->second.end()) {
                            if (metric_it->second == UINT64_MAX) {
                                // Unsupported event - output empty for NA
                            } else {
                                *out << metric_it->second;
                            }
                        }
                        // else: empty (no value for this metric)
                    }
                    // else: empty (no data for this host)
                }
                // else: empty (no data for this timestamp)
            }
        }

        // Output derived metric: clock_rate_GHz for each host
        if (!perf_metric_names.empty()) {
            for (const auto& hostname : sorted_perf_hosts) {
                *out << ",";
                auto ts_it = perf_data_map.find(timestamp);
                if (ts_it != perf_data_map.end()) {
                    auto host_it = ts_it->second.find(hostname);
                    if (host_it != ts_it->second.end()) {
                        // Find cycles and cpu_clock for this host/timestamp
                        auto cycles_it = host_it->second.find("cycles");
                        auto cpu_clock_it = host_it->second.find("cpu_clock");
                        
                        if (cycles_it != host_it->second.end() && cpu_clock_it != host_it->second.end() &&
                            cycles_it->second != UINT64_MAX && cpu_clock_it->second != UINT64_MAX &&
                            cpu_clock_it->second > 0) {
                            double clock_rate_ghz = static_cast<double>(cycles_it->second) / 
                                                   (static_cast<double>(cpu_clock_it->second) / 1000.0);
                            *out << std::fixed << std::setprecision(2) << clock_rate_ghz;
                        }
                        // else: empty (cannot compute clock_rate)
                    }
                    // else: empty (no data for this host)
                }
                // else: empty (no data for this timestamp)
            }
        }

        // Output NCCL metric columns per (host, nic) pair
        if (has_nccl) {
            struct NcclWideCol {
                const char* col_name;
                const char* num_field;
                const char* den_field;  // nullptr = emit num_field directly
            };
            static const NcclWideCol wide_cols[] = {
                {"nccl_send_bytes",      "nccl_send_bytes",       nullptr},
                {"nccl_send_count",      "nccl_send_count",       nullptr},
                {"nccl_send_avg_dur_us", "nccl_send_duration_us", "nccl_send_count"},
                {"nccl_recv_bytes",      "nccl_recv_bytes",       nullptr},
                {"nccl_recv_count",      "nccl_recv_count",       nullptr},
                {"nccl_recv_avg_dur_us", "nccl_recv_duration_us", "nccl_recv_count"},
            };
            for (const auto& mc : wide_cols) {
                for (const auto& [hostname, nic_name] : sorted_pairs) {
                    *out << ",";
                    auto it = nccl_deltas.find(std::make_tuple(timestamp, hostname, nic_name));
                    if (it != nccl_deltas.end()) {
                        auto get = [&](const char* name) -> uint64_t {
                            auto mit = it->second.find(name);
                            return mit != it->second.end() ? mit->second : 0;
                        };
                        if (mc.den_field == nullptr) {
                            uint64_t v = get(mc.num_field);
                            if (v > 0) *out << v;
                        } else {
                            uint64_t num = get(mc.num_field), den = get(mc.den_field);
                            if (den > 0)
                                *out << std::fixed << std::setprecision(1) << (double)num / den;
                        }
                    }
                }
            }
        }

        *out << std::endl;
    }

    if (file_stream.is_open()) {
        file_stream.close();
    }

    return filtered_timestamps.size();
}

void print_summary(const std::map<long, std::vector<uint64_t>>& initial_counters,
                        const std::map<long, std::vector<uint64_t>>& final_counters,
                        double time_interval) {
    CounterLevel level = get_counter_level();
    if (level == QUIET) return; // quiet: no output
    if (is_json_enabled()) {
        print_json_summary(initial_counters, final_counters, time_interval);
    } else {
        print_human_summary(initial_counters, final_counters, time_interval);
    }
}

void node_handler(bool is_first, bool is_last, long procid, long node_count, int cmd_start_idx, int argc, char* argv[], const std::string& experiment_name) {
    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    std::vector<std::string> cmd_args(argv + cmd_start_idx, argv + argc);

    /*
     * Phase 1: ALL nodes collect initial counters and run command concurrently
     */
    std::vector<uint64_t> initial = read_all_counters();

    // Time-series collection setup
    NodeTimeSeries node_ts;
    node_ts.hostname = hostname;
    node_ts.procid = procid;

    std::atomic<bool> sampling_active(false);
    std::thread* sampler = nullptr;

    // Prepare and add an initial per-NIC sample into node_ts so we always have per-NIC data
    if (is_detailed_enabled()) {
        TimeSample init_sample;
        // Get current UNIX timestamp in seconds with millisecond precision (3 decimal places)
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        double timestamp = std::chrono::duration<double>(duration).count();
        init_sample.timestamp = std::round(timestamp * 1000.0) / 1000.0;
        init_sample.nic_counters = read_all_counters_per_nic();

        // NEW: Use registry to collect initial metrics
        MetricRegistry& registry = MetricRegistry::instance();
        for (const auto& source_name : registry.source_names()) {
            auto* source = registry.lookup(source_name);
            if (!source) {
                continue;
            }
            try {
                auto src_sample = source->read();
                for (const auto& [entity_id, metrics] : src_sample.entities) {
                    init_sample.sources[source_name][entity_id] = metrics;
                }
            } catch (...) {
                // Keep backward compat data if a source fails
            }
        }
        
        node_ts.samples.push_back(init_sample);
    }

    // Start time-series sampling if detailed mode is enabled
    if (is_detailed_enabled()) {
        int interval = get_sample_interval();
        sampling_active.store(true);
        sampler = new std::thread(sampling_thread, std::ref(sampling_active), std::ref(node_ts), interval);
    }

    // Run the command
    CommandResult cmd_result = run_command(cmd_args);
    if (cmd_result.execution_time < 0) exit(1);
    node_ts.execution_time = cmd_result.execution_time;
    node_ts.latency_timestamps = cmd_result.latency_timestamps;
    node_ts.repeat_numbers = cmd_result.repeat_numbers;

    // Stop time-series sampling
    if (sampling_active.load()) {
        sampling_active.store(false);
        if (sampler) {
            sampler->join();
            delete sampler;
        }
    }

    // Add final per-NIC sample
    if (is_detailed_enabled()) {
        TimeSample final_sample;
        // Get current UNIX timestamp in seconds with millisecond precision (3 decimal places)
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        double timestamp = std::chrono::duration<double>(duration).count();
        final_sample.timestamp = std::round(timestamp * 1000.0) / 1000.0;
        final_sample.nic_counters = read_all_counters_per_nic();

        // NEW: Use registry to collect final metrics
        MetricRegistry& registry = MetricRegistry::instance();
        for (const auto& source_name : registry.source_names()) {
            auto* source = registry.lookup(source_name);
            if (!source) {
                continue;
            }
            try {
                auto src_sample = source->read();
                for (const auto& [entity_id, metrics] : src_sample.entities) {
                    final_sample.sources[source_name][entity_id] = metrics;
                }
            } catch (...) {
                // Keep backward compat data if a source fails
            }
        }
        
        node_ts.samples.push_back(final_sample);
    }

    std::vector<uint64_t> final = read_all_counters();

    /*
     * Phase 2: Chain aggregation (happens AFTER all nodes have finished running)
     */
    AggregatedStats agg;
    if (!is_last) {
        receive_aggregated_stats(agg);
    }
    agg.hostnames.push_back(hostname);
    agg.procids.push_back(procid);
    agg.all_initial.push_back(initial);
    agg.all_final.push_back(final);
    agg.times.push_back(node_ts.execution_time);

    // Always add node_ts so that latency_timestamps (from the subordinate program)
    // are available for CSV output even when detailed NIC/counter sampling is disabled.
    // When GATHER_CXI_DETAILED=0, node_ts.samples will be empty but
    // node_ts.latency_timestamps will still carry the per-iteration latency data.
    agg.time_series.push_back(node_ts);

    if (is_first) {
        /*
         * First node: print summary instead of forwarding
         */
        if (is_logging_enabled()) {
            std::cout << "Aggregated statistics from " << agg.hostnames.size() << " nodes." << std::endl;
        }

        // Print output based on format
        OutputFormat format = get_output_format();
        if (format == OutputFormat::CSV_WIDE || format == OutputFormat::CSV_TALL) {
            // Generate filenames if experiment_name is provided
            std::string csv_filename;
            std::string md_filename;
            if (!experiment_name.empty()) {
                // Create runlogs directory
                system("mkdir -p runlogs");
                csv_filename = "runlogs/" + experiment_name + ".csv";
                md_filename = "runlogs/" + experiment_name + ".md";
            }

            // Write CSV
            size_t row_count = 0;
            if (format == OutputFormat::CSV_WIDE) {
                row_count = print_csv_wide(agg, csv_filename);
            } else {
                row_count = print_csv_tall(agg, csv_filename);
            }

            // Generate markdown if experiment_name provided
            if (!experiment_name.empty()) {
                std::string csv_basename = experiment_name + ".csv";
                generate_markdown_metadata(md_filename, agg, cmd_args, csv_basename, row_count,
                                           format == OutputFormat::CSV_WIDE);
                std::cerr << "Results written to: " << csv_filename << " and " << md_filename << std::endl;
            }
        } else if (is_detailed_enabled() || format == OutputFormat::JSON) {
            print_detailed_json_summary(agg, cmd_args);
        } else {
            // Standard text output
            std::map<long, std::vector<uint64_t>> initial_counters;
            std::map<long, std::vector<uint64_t>> final_counters;
            for (size_t i = 0; i < agg.procids.size(); ++i) {
                initial_counters[agg.procids[i]] = agg.all_initial[i];
                final_counters[agg.procids[i]] = agg.all_final[i];
            }
            double avg_time = std::accumulate(agg.times.begin(), agg.times.end(), 0.0) / agg.times.size();
            print_summary(initial_counters, final_counters, avg_time);
        }
    } else {
        /*
         * Non-first nodes: forward upstream
         */
        send_to_previous(agg, procid);
    }
}
