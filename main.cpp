/*
 * gather_cxi_counters - A tool for collecting and aggregating CXI (Cassini) network counters
 * across multiple compute nodes in a SLURM job environment.
 *
 * Functionality:
 * This program is designed to run on each node of a multi-node SLURM job. It collects
 * CXI network performance counters before and after executing a user-specified command,
 * then aggregates the data across all nodes and produces a summary report showing
 * counter differences. The program handles single-node jobs directly and uses a
 * distributed chain topology for multi-node jobs to efficiently collect and aggregate
 * statistics without requiring a central coordinator.
 *
 * Intent:
 * The primary purpose is to measure the network impact and performance characteristics
 * of parallel applications or commands running across multiple nodes. By capturing
 * CXI counters (such as packet counts, error rates, and throughput metrics) before
 * and after command execution, users can quantify the network resources consumed
 * by their workloads. This is particularly useful for:
 * - Performance analysis of MPI applications
 * - Network benchmarking and characterization
 * - Identifying network bottlenecks in distributed computing
 * - Monitoring resource usage in HPC environments
 *
 * Design Overview:
 * The program uses a hierarchical, chain-based communication pattern for multi-node
 * data aggregation:
 *
 * Single Node Operation:
 * - Reads initial CXI counters
 * - Executes the specified command
 * - Reads final CXI counters
 * - Calculates and prints counter differences with timing information
 *
 * Multi-Node Operation (Chain Topology):
 * - Nodes are ordered by SLURM_PROCID (0 = first, N-1 = last)
 * - Last node: Collects local counters, runs command, sends aggregated data to previous
 * - Middle nodes: Receive aggregated data from next node, add local counters, forward
 * - First node: Receives final aggregated data, calculates averages, prints summary
 *
 * Key Design Decisions:
 * - Chain topology minimizes network traffic and avoids single points of failure
 * - ZeroMQ provides reliable, asynchronous inter-node communication
 * - Static linking of ZeroMQ ensures portability across different system configurations
 * - Environment variable control for optional logging to reduce output noise
 * - Graceful handling of empty node lists and command execution failures
 *
 * Usage:
 * srun -N <num_nodes> ./gather_cxi_counters <command> [args...]
 *
 * Environment Variables:
 * - GATHER_CXI_LOG=1 : Enable verbose logging of inter-node communication
 * - GATHER_CXI_JSON=1 : Output summary in JSON format instead of human-readable table
 * - COUNTER_LEVEL=<0-5> : Control output verbosity (0=quiet, 1=default, 2=summary, 3=on_error, 4=ALL_ON_ERROR, 5=all)
 *
 * Output:
 * A tabular summary showing hostname, process ID, counter names, initial values,
 * final values, differences, and execution time statistics.
 */

#include "main.h"
#include "json/json.hpp"
#include "counter_collection.h"
#include "counter_aggregation.h"
#include "zmq_transfer.h"
#include "utils.h"
#include "metric_registry.h"
#include "metric_source_nic.h"
#include "metric_source_perf.h"
#include "metric_source_nccl.h"

/*
 * Main entry point that orchestrates the CXI counter collection and aggregation.
 * Determines the node's role in the SLURM job based on environment variables,
 * handles special cases (single node, multiple tasks per node), and delegates
 * to appropriate handlers for data collection and aggregation.
 * Implementation: Parses SLURM_NODEID and SLURM_LOCALID, gets node list,
 * filters out non-participating tasks, handles single-node vs multi-node logic,
 * and calls node_handler() with appropriate flags for chain aggregation.
 */
int main(int argc, char* argv[]) {
    // Apply CPU pinning early (before any metric/thread work)
    apply_cpu_pin();

    // Initialise counter filtering (GATHER_CXI_COUNTERS env var) before
    // any counter_names use.  Must happen before NicMetricSource construction.
    init_counter_filter();

    // Parse experiment name flag (-e)
    std::string experiment_name;
    int cmd_start_idx = 1;
    if (argc >= 3 && strcmp(argv[1], "-e") == 0) {
        experiment_name = argv[2];
        cmd_start_idx = 3;
    }

    /*
     * Get SLURM node ID
     */
    const char* procid_env = getenv("SLURM_NODEID");
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    long procid = 0;
    if (procid_env) {
        char* endptr = nullptr;
        procid = strtol(procid_env, &endptr, 10);
        if (endptr == procid_env || *endptr != '\0') {
            procid = 0;
        }
    }
    std::vector<std::string> nodes = get_node_list_from_scontrol();
    if (nodes.empty()) {
        // Not running under SLURM or single standalone execution
        // Handle as single-node case
        gethostname(hostname, sizeof(hostname));
        nodes.push_back(hostname);
        procid = 0;
    }
    long node_count = nodes.size();

    /*
     * Register metric sources with the plugin registry
     * This happens on every node before any sampling begins
     */
    MetricRegistry& registry = MetricRegistry::instance();
    registry.register_source("nic", std::make_unique<NicMetricSource>());

    const char* os_metrics_env = getenv("GATHER_CXI_OS_METRICS");
    if (os_metrics_env && std::string(os_metrics_env) == "1") {
        registry.register_source("perf", std::make_unique<PerfMetricSource>());
    }

    // NCCL/RCCL proxy-step profiler source.
    // Enabled by: GATHER_CXI_NCCL_PROFILER=1
    // Requires LD_LIBRARY_PATH to include the directory containing
    //   librccl-profiler-gather.so  (built in nccl_profiler/ subdirectory).
    const char* nccl_profiler_env = getenv("GATHER_CXI_NCCL_PROFILER");
    if (nccl_profiler_env && std::string(nccl_profiler_env) == "1") {
        registry.register_source("nccl", std::make_unique<NcclMetricSource>());
    }
    
    if (!registry.init_all()) {
        std::cerr << "Warning: Failed to initialize some metric sources" << std::endl;
        // Continue anyway - NIC collection is critical, others are optional
    }

    // Open and cache sysfs file descriptors for fast repeated reads
    init_counter_fds();

    /*
     * If multiple tasks per node, only local task 0 participates in aggregation; others just run command
     */
    const char* localid_env = getenv("SLURM_LOCALID");
    if (localid_env) {
        long localid = strtol(localid_env, nullptr, 10);
        if (localid > 0) {
            std::vector<std::string> cmd_args(argv + cmd_start_idx, argv + argc);
            (void)run_command(cmd_args);
            return 0;
        }
    }

    /*
     * If more tasks than nodes, extra tasks run the command but don't collect stats
     */
    if (procid >= node_count) {
        std::vector<std::string> cmd_args(argv + cmd_start_idx, argv + argc);
        run_command(cmd_args);
        return 0;
    }

    /*
     * Handle single-node case: run command, collect counters before/after, print summary
     */
    if (node_count == 1) {
        // Always use node_handler: it respects GATHER_CXI_OUTPUT_FORMAT and writes
        // the CSV (including latency-only output when GATHER_CXI_DETAILED=0).
        node_handler(true, true, 0, 1, cmd_start_idx, argc, argv, experiment_name);

        cleanup_counter_fds();
        registry.cleanup_all();
        return 0;
    }

    /*
     * Last node: start chain aggregation, run command, send stats to previous node
     */
    if (procid == node_count - 1) {
        node_handler(false, true, procid, node_count, cmd_start_idx, argc, argv, experiment_name);
        cleanup_counter_fds();
        registry.cleanup_all();
        return 0;
    }
    /*
     * First node: receive aggregated stats and print summary
     */
    else if (procid == 0) {
        node_handler(true, false, procid, node_count, cmd_start_idx, argc, argv, experiment_name);
        cleanup_counter_fds();
        registry.cleanup_all();
        return 0;
    }
    /*
     * Middle nodes: receive stats, add own, forward to previous node
     */
    else {
        node_handler(false, false, procid, node_count, cmd_start_idx, argc, argv, experiment_name);
        cleanup_counter_fds();
        registry.cleanup_all();
        return 0;
    }
}
