/**
 * metric_source_nic.cpp - Implementation of NIC metric source
 *
 * Wraps the existing read_all_counters_per_nic() function in the plugin interface.
 */

#include "metric_source_nic.h"
#include "counter_collection.h"
#include <ctime>

/**
 * Constructor: Initialize metadata for all 25 NIC metrics
 */
NicMetricSource::NicMetricSource() {
    // Build metadata dynamically from active counter_names
    // (which may have been filtered by GATHER_CXI_COUNTERS)
    for (const auto& name : counter_names) {
        metadata_.push_back({name, name, "count", true, MetricScope::PER_NIC});
    }
}

/**
 * Get the metric scope (PER_NIC)
 */
MetricScope NicMetricSource::scope() const {
    return MetricScope::PER_NIC;
}

/**
 * Get the metadata for all metrics
 */
const std::vector<MetricMetadata>& NicMetricSource::metadata() const {
    return metadata_;
}

/**
 * Initialize the NIC metric source
 * Since we read from files, no special initialization needed
 * Returns true to indicate success
 */
bool NicMetricSource::init() {
    // No resources to initialize for file-based collection
    return true;
}

/**
 * Cleanup the NIC metric source
 * Since we read from files, no cleanup needed
 */
void NicMetricSource::cleanup() {
    has_last_sample_ = false;
}

/**
 * Read current NIC metrics
 * Calls the existing read_all_counters_per_nic() function
 * Returns a sample with timestamp and per-NIC counter values
 */
MetricSourceSample NicMetricSource::read() {
    auto nic_data = read_all_counters_per_nic();
    
    MetricSourceSample sample;
    sample.timestamp_ms = static_cast<uint64_t>(std::time(nullptr) * 1000.0);
    sample.scope = MetricScope::PER_NIC;
    
    // Convert the map of NIC name -> vector of values into the plugin format
    // NIC name -> metric name -> value
    for (const auto& [nic_name, counter_values] : nic_data) {
        auto& nic_entity = sample.entities[nic_name];
        
        // Fill in raw counter values
        for (size_t i = 0; i < counter_values.size() && i < counter_names.size(); ++i) {
            nic_entity[counter_names[i]] = counter_values[i];
        }
        
        // Compute derived metrics (blocked ratios) using name lookup
        // (indices may vary when counter filtering is active)
        auto find_val = [&](const std::string& name) -> uint64_t {
            for (size_t i = 0; i < counter_names.size() && i < counter_values.size(); ++i) {
                if (counter_names[i] == name) return counter_values[i];
            }
            return 0;
        };
        uint64_t posted_blocked = find_val("parbs_tarb_pi_posted_blocked_cnt");
        uint64_t nonposted_blocked = find_val("parbs_tarb_pi_non_posted_blocked_cnt");
        nic_entity["posted_blocked_ratio"] = posted_blocked;
        nic_entity["nonposted_blocked_ratio"] = nonposted_blocked;
    }
    
    return sample;
}
