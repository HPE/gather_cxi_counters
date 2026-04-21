/**
 * metric_source_perf.h - OS/perf metric source plugin
 *
 * Collects perf_event counters for per-process metrics (inherit=1).
 * Uses perf_event_open to capture hardware and software events.
 */

#ifndef METRIC_SOURCE_PERF_H
#define METRIC_SOURCE_PERF_H

#include "metric_source.h"
#include <string>
#include <vector>
#include <set>

/**
 * Perf metric source - OS and CPU performance counters
 *
 * Scope: PER_HOST (one value per host per timestamp)
 */
class PerfMetricSource : public MetricSource {
public:
    PerfMetricSource();
    ~PerfMetricSource() override = default;

    // MetricSource interface
    MetricScope scope() const override;
    const std::vector<MetricMetadata>& metadata() const override;
    bool init() override;
    void cleanup() override;
    MetricSourceSample read() override;

    // Diagnostics
    const std::set<std::string>& unsupported_events() const;

private:
    struct PerfEventConfig {
        std::string name;
        std::string description;
        std::string unit;
        uint32_t type;
        uint64_t config;
        bool always_unsupported;
    };

    int open_event(const PerfEventConfig& cfg);
    uint64_t read_event_value(int fd) const;
    std::string hostname_;

    std::vector<MetricMetadata> metadata_;
    std::vector<PerfEventConfig> configs_;
    std::vector<int> fds_;
    std::set<std::string> unsupported_;
};

#endif // METRIC_SOURCE_PERF_H
