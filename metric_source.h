/**
 * metric_source.h - Plugin interface for metric collection sources
 *
 * Defines the MetricSource abstract base class that all metric collectors must implement.
 * This enables pluggable metric collection for NIC counters, OS/perf metrics, fabric metrics, etc.
 *
 * Design:
 * - MetricScope: Identifies granularity level (PER_NIC, PER_HOST, PER_CPU, PER_SWITCH)
 * - MetricMetadata: Describes a metric (name, description, unit, cumulative flag)
 * - MetricSource: Pure virtual interface with init/cleanup/read lifecycle
 * - TimeSample: Contains timestamp and per-source metric values
 */

#ifndef METRIC_SOURCE_H
#define METRIC_SOURCE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <memory>

/**
 * Enum to identify the scope/granularity of metrics from a source
 */
enum class MetricScope {
    PER_NIC,     // One value per NIC per timestamp (e.g., hsn0, hsn1)
    PER_HOST,    // One value per host per timestamp (aggregated across NICs)
    PER_CPU,     // One value per CPU/socket per timestamp (future extension)
    PER_SWITCH   // One value per fabric switch per timestamp (future extension)
};

/**
 * Metadata describing a single metric
 */
struct MetricMetadata {
    std::string name;           // Metric name (e.g., "hni_rx_paused_0", "cache_misses")
    std::string description;    // Human-readable description
    std::string unit;           // Unit of measurement (e.g., "count", "bytes", "cycles")
    bool is_cumulative;         // True if counter increments; false if snapshot
    MetricScope scope;          // Granularity (PER_NIC, PER_HOST, etc.)
};

/**
 * A time sample from a single metric source
 * Contains metrics grouped by entity (NIC name, hostname, CPU ID, etc.)
 */
struct MetricSourceSample {
    uint64_t timestamp_ms;      // Timestamp in milliseconds since epoch
    MetricScope scope;          // Scope of this sample
    
    // For PER_NIC: source_id = NIC name (e.g., "hsn0")
    // For PER_HOST: source_id = hostname (e.g., "node001")
    // For PER_CPU: source_id = CPU ID (e.g., "0", "1")
    // For PER_SWITCH: source_id = switch ID (e.g., "switch01")
    std::map<std::string, std::map<std::string, uint64_t>> entities;  // entity_id -> metric_name -> value
};

/**
 * Pure virtual interface for metric sources
 * All metric collectors (NIC, Perf, Fabric, etc.) must implement this interface
 */
class MetricSource {
public:
    virtual ~MetricSource() = default;

    /**
     * Get the scope of metrics from this source
     */
    virtual MetricScope scope() const = 0;

    /**
     * Get the list of metrics this source provides
     * Must return at least one metric
     */
    virtual const std::vector<MetricMetadata>& metadata() const = 0;

    /**
     * Initialize the metric source
     * Opens file descriptors, connects to services, allocates resources, etc.
     * Returns true on success, false on failure
     *
     * Should be idempotent - calling init() twice should be safe
     */
    virtual bool init() = 0;

    /**
     * Cleanup and release resources
     * Closes file descriptors, disconnects from services, frees memory, etc.
     * Should be idempotent - calling cleanup() twice should be safe
     */
    virtual void cleanup() = 0;

    /**
     * Read current metrics from this source
     * Returns a sample with timestamp and metric values for all entities
     *
     * Precondition: init() must be called first
     * Returns sample with timestamp and metric values keyed by entity_id
     *
     * For unavailable metrics, use UINT64_MAX as sentinel value
     */
    virtual MetricSourceSample read() = 0;
};

#endif // METRIC_SOURCE_H
