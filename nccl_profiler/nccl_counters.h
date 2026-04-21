/*
 * nccl_counters.h — Shared-memory per-channel counters
 *
 * The NCCL profiler plugin (librccl-profiler-gather.so, loaded inside the
 * RCCL workload process) and the NcclMetricSource listener (inside the
 * gather_cxi_counters sidecar process) share a flat array of per-channel
 * cumulative counters via POSIX shared memory (shm_open/mmap).
 *
 * There is NO ring buffer.  The profiler increments the counters directly
 * with atomic fetch_add on each ProxyStep completion; the sidecar reads
 * them with relaxed loads once per 200 ms sampling interval.  The existing
 * delta-compute layer differences successive reads to get per-interval
 * deltas — identical to how CXI NIC counters work.
 *
 * Hot-path cost: ~90 ns per proxy step (1 vDSO clock_gettime + 3 fetch_add
 * + 1 CAS), ~7 µs per allreduce (~76 steps).  No system calls, no kernel
 * transitions, no IPC messages.
 *
 * On x86_64, atomic<uint64_t> is lock-free (uses hardware LOCK XADD / CMPXCHG)
 * and is safe across processes sharing the same physical pages via MAP_SHARED.
 *
 * The shared-memory segment name is communicated via the environment
 * variable GATHER_CXI_SHM_PATH (e.g. "/nccl_gather_<pid>"), set by
 * NcclMetricSource::init() before the child process is fork/exec'd.
 */

#ifndef NCCL_COUNTERS_H
#define NCCL_COUNTERS_H

#include <stdint.h>
#include <atomic>

// Maximum RCCL channels.  NCCL caps at 32; RCCL typically uses 4-8.
static constexpr int MAX_NCCL_CHANNELS = 32;

// ---------------------------------------------------------------------------
// Per-channel cumulative counters.
// Aligned to 64 bytes (one cache line) so different channels never share a
// cache line.  8 × 8 bytes = 64 bytes exactly.
// ---------------------------------------------------------------------------
struct alignas(64) ShmChannelCounters {
    std::atomic<uint64_t> send_count;        // completed send proxy steps
    std::atomic<uint64_t> send_bytes;        // total bytes sent
    std::atomic<uint64_t> send_duration_ns;  // sum of step durations (MONOTONIC)
    std::atomic<uint64_t> send_max_dur_ns;   // running max step duration (ns)
    std::atomic<uint64_t> recv_count;
    std::atomic<uint64_t> recv_bytes;
    std::atomic<uint64_t> recv_duration_ns;
    std::atomic<uint64_t> recv_max_dur_ns;
};

static_assert(sizeof(ShmChannelCounters) == 64,
              "ShmChannelCounters must be exactly one cache line");
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "uint64_t atomics must be lock-free for cross-process shm");

// ---------------------------------------------------------------------------
// Shared-memory segment mapped into both the RCCL workload process and the
// gather_cxi_counters sidecar.  Total size: 32 × 64 = 2048 bytes.
// ---------------------------------------------------------------------------
struct alignas(64) ShmNcclCounters {
    ShmChannelCounters channels[MAX_NCCL_CHANNELS];
};

// ---------------------------------------------------------------------------
// Helper: atomically update a running maximum.
// ---------------------------------------------------------------------------
inline void atomic_max_u64(std::atomic<uint64_t>& a, uint64_t val) {
    uint64_t cur = a.load(std::memory_order_relaxed);
    while (val > cur) {
        if (a.compare_exchange_weak(cur, val, std::memory_order_relaxed))
            return;
    }
}

#endif // NCCL_COUNTERS_H
