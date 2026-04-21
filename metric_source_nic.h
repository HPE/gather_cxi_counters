/**
 * metric_source_nic.h - NIC metric source plugin
 *
 * Collects network interface counters from Cassini (CXI) hardware.
 * Wraps the existing read_all_counters_per_nic() function in the plugin interface.
 * Also computes derived metrics like transmitted/posted blocked ratios.
 */

#ifndef METRIC_SOURCE_NIC_H
#define METRIC_SOURCE_NIC_H

#include "metric_source.h"
#include <vector>
#include <string>

/**
 * NIC metric source - Cassini (CXI) network interface counters
 * 
 * Metrics: 25 raw NIC counters (per-NIC basis)
 * Derived: 2 computed metrics (posted_blocked_ratio, nonposted_blocked_ratio)
 * 
 * Scope: PER_NIC (separate values for hsn0, hsn1, hsn2, hsn3, etc.)
 */
class NicMetricSource : public MetricSource {
public:
    NicMetricSource();
    ~NicMetricSource() override = default;

    // MetricSource interface implementation
    MetricScope scope() const override;
    const std::vector<MetricMetadata>& metadata() const override;
    bool init() override;
    void cleanup() override;
    MetricSourceSample read() override;

private:
    // Sentinel value for unavailable metrics (used in later extensions like perf)
    static constexpr uint64_t NA_SENTINEL = UINT64_MAX;

    // Metadata for all NIC metrics
    std::vector<MetricMetadata> metadata_;
    
    // Last sample for computing deltas
    MetricSourceSample last_sample_;
    bool has_last_sample_ = false;
};

#endif // METRIC_SOURCE_NIC_H
