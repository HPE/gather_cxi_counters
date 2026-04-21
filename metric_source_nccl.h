/**
 * metric_source_nccl.h - NCCL/RCCL proxy-step metric source
 *
 * Collects per-channel network transfer statistics from a running RCCL
 * workload via shared-memory counters (ShmNcclCounters).  The companion
 * shared library nccl_profiler/librccl-profiler-gather.so is dlopen'd by
 * RCCL when
 *
 *   NCCL_PROFILER_PLUGIN=gather
 *
 * is set in the environment.  NcclMetricSource::init() sets that variable
 * (and others) before the child command is fork/exec'd, so the child
 * inherits the full configuration automatically.
 *
 * Lifetime
 * --------
 *   init()     — discovers HSN NICs from /sys/class/net/, creates a POSIX shm
 *                segment with ShmNcclCounters, sets NCCL_PROFILER_PLUGIN /
 *                GATHER_CXI_SHM_PATH.
 *
 *   read()     — reads the current counter values from shared memory with
 *                plain loads (no drain loop, no IPC), maps channels → NICs,
 *                returns a MetricSourceSample.  The delta-compute layer
 *                differences successive reads to get per-interval deltas.
 *
 *   cleanup()  — unmaps and unlinks the shm segment.
 *
 * Columns added to the runlog when GATHER_CXI_NCCL_PROFILER=1
 * ------------------------------------------------------------
 *   nccl_send_bytes, nccl_send_count, nccl_send_avg_dur_us (p2p send latency)
 *   nccl_recv_bytes, nccl_recv_count, nccl_recv_avg_dur_us (p2p recv latency)
 */

#ifndef METRIC_SOURCE_NCCL_H
#define METRIC_SOURCE_NCCL_H

#include "metric_source.h"
#include "nccl_profiler/nccl_counters.h"   // ShmNcclCounters

#include <string>
#include <vector>
#include <map>
#include <cstdint>

/**
 * NCCL metric source — reads per-channel counters from shared memory.
 *
 * Scope: PER_NIC (entity key is "hsn<N>" from nic_for_channel()).
 */
class NcclMetricSource : public MetricSource {
public:
    NcclMetricSource();
    ~NcclMetricSource() override;

    // MetricSource interface
    MetricScope scope() const override;
    const std::vector<MetricMetadata>& metadata() const override;
    bool init() override;
    void cleanup() override;
    MetricSourceSample read() override;

private:
    // -----------------------------------------------------------------------
    // Shared-memory direct counters (POSIX shm)
    // -----------------------------------------------------------------------
    ShmNcclCounters* shm_counters_ = nullptr;
    int              shm_fd_       = -1;
    std::string      shm_name_;     // POSIX shm name, e.g. "/nccl_gather_<pid>"

    // -----------------------------------------------------------------------
    // Identity
    // -----------------------------------------------------------------------
    std::string hostname_;

    // -----------------------------------------------------------------------
    // NIC discovery (populated in init())
    // -----------------------------------------------------------------------
    std::vector<std::string> nics_;

    // -----------------------------------------------------------------------
    // Metadata (fixed set of metric names, declared once)
    // -----------------------------------------------------------------------
    std::vector<MetricMetadata> metadata_;

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------
    void discover_nics_from_sysfs();
    std::string nic_for_channel(int ch) const;
};

#endif // METRIC_SOURCE_NCCL_H
