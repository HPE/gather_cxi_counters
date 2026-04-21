/**
 * metric_source_nccl.cpp
 *
 * NcclMetricSource implementation.
 *
 * See metric_source_nccl.h for the full design description.
 */

#include "metric_source_nccl.h"
#include "nccl_profiler/nccl_counters.h"

#include <sys/mman.h>
#include <fcntl.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <dirent.h>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

NcclMetricSource::NcclMetricSource() {
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0) {
        hostname_ = buf;
    } else {
        hostname_ = "unknown";
    }

    // Build the fixed metadata list.  Scope is PER_NIC; entity keys returned
    // from read() are NIC names (e.g. "hsn0") from nic_for_channel().
    const MetricScope sc = MetricScope::PER_NIC;

    auto add = [&](const char* name, const char* desc, const char* unit) {
        metadata_.push_back({name, desc, unit, /*is_cumulative=*/true, sc});
    };

    add("nccl_send_count",       "Completed RCCL proxy send steps",             "count");
    add("nccl_send_bytes",       "Total bytes sent via RCCL proxy",             "bytes");
    add("nccl_send_duration_us", "Cumulative send duration (µs)",               "microseconds");
    add("nccl_send_max_dur_us",  "Maximum single send-step duration (µs)",      "microseconds");
    add("nccl_recv_count",       "Completed RCCL proxy receive steps",          "count");
    add("nccl_recv_bytes",       "Total bytes received via RCCL proxy",         "bytes");
    add("nccl_recv_duration_us", "Cumulative receive duration (µs)",            "microseconds");
    add("nccl_recv_max_dur_us",  "Maximum single receive-step duration (µs)",   "microseconds");
}

NcclMetricSource::~NcclMetricSource() {
    // cleanup() should already have been called, but guard defensively.
    if (shm_counters_) { munmap(shm_counters_, sizeof(ShmNcclCounters)); shm_counters_ = nullptr; }
    if (shm_fd_ >= 0) { close(shm_fd_); shm_fd_ = -1; }
    if (!shm_name_.empty()) { shm_unlink(shm_name_.c_str()); shm_name_.clear(); }
}

// ---------------------------------------------------------------------------
// MetricSource interface — scope / metadata
// ---------------------------------------------------------------------------

MetricScope NcclMetricSource::scope() const {
    return MetricScope::PER_NIC;
}

const std::vector<MetricMetadata>& NcclMetricSource::metadata() const {
    return metadata_;
}

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------

bool NcclMetricSource::init() {
    // Build a unique POSIX shm name from our PID so concurrent srun tasks
    // on the same node don't collide.
    pid_t pid = getpid();
    shm_name_ = "/nccl_gather_" + std::to_string(pid);

    // --- Set environment variables that the forked child will inherit -------
    setenv("NCCL_PROFILER_PLUGIN",  "gather",          /*overwrite=*/1);
    setenv("GATHER_CXI_SHM_PATH",   shm_name_.c_str(), 1);

    // --- Create and map the shared-memory counter struct --------------------
    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0600);
    if (shm_fd_ < 0) {
        std::cerr << "[NcclMetricSource] shm_open(" << shm_name_
                  << ") failed: " << strerror(errno) << std::endl;
        return false;
    }

    if (ftruncate(shm_fd_, (off_t)sizeof(ShmNcclCounters)) < 0) {
        std::cerr << "[NcclMetricSource] ftruncate failed: "
                  << strerror(errno) << std::endl;
        close(shm_fd_); shm_fd_ = -1;
        shm_unlink(shm_name_.c_str()); shm_name_.clear();
        return false;
    }

    void* ptr = mmap(nullptr, sizeof(ShmNcclCounters),
                     PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (ptr == MAP_FAILED) {
        std::cerr << "[NcclMetricSource] mmap failed: "
                  << strerror(errno) << std::endl;
        close(shm_fd_); shm_fd_ = -1;
        shm_unlink(shm_name_.c_str()); shm_name_.clear();
        return false;
    }

    // Placement-new zero-initialises all atomic counters.
    shm_counters_ = new (ptr) ShmNcclCounters{};

    // Discover HSN NICs now so entity keys in read() use NIC names.
    discover_nics_from_sysfs();
    return true;
}

// ---------------------------------------------------------------------------
// read()   — called every sampling interval while the workload runs
// ---------------------------------------------------------------------------

MetricSourceSample NcclMetricSource::read() {
    // Build a snapshot MetricSourceSample by reading the shared-memory
    // counters directly.  No drain loop, no IPC — just plain loads.
    MetricSourceSample sample;
    sample.scope        = MetricScope::PER_NIC;
    sample.timestamp_ms = (uint64_t)(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    if (shm_counters_) {
        for (int ch = 0; ch < MAX_NCCL_CHANNELS; ++ch) {
            auto& c = shm_counters_->channels[ch];
            uint64_t sc = c.send_count.load(std::memory_order_relaxed);
            uint64_t rc = c.recv_count.load(std::memory_order_relaxed);
            if (sc == 0 && rc == 0) continue;  // skip unused channels

            std::string nic = nic_for_channel(ch);
            auto& m = sample.entities[nic];
            // Additive metrics: sum across channels mapping to the same NIC
            m["nccl_send_count"]       += sc;
            m["nccl_send_bytes"]       += c.send_bytes.load(std::memory_order_relaxed);
            m["nccl_send_duration_us"] += c.send_duration_ns.load(std::memory_order_relaxed) / 1000;
            m["nccl_send_max_dur_us"]   = std::max(m["nccl_send_max_dur_us"],
                                                    c.send_max_dur_ns.load(std::memory_order_relaxed) / 1000);
            m["nccl_recv_count"]       += rc;
            m["nccl_recv_bytes"]       += c.recv_bytes.load(std::memory_order_relaxed);
            m["nccl_recv_duration_us"] += c.recv_duration_ns.load(std::memory_order_relaxed) / 1000;
            m["nccl_recv_max_dur_us"]   = std::max(m["nccl_recv_max_dur_us"],
                                                    c.recv_max_dur_ns.load(std::memory_order_relaxed) / 1000);
        }
    }

    return sample;
}

// ---------------------------------------------------------------------------
// cleanup()   — called after the workload finishes
// ---------------------------------------------------------------------------

void NcclMetricSource::cleanup() {
    // With direct shm counters, the values are always up-to-date — no drain
    // needed.  Give the workload a moment to finish its last proxy ops.
    if (shm_counters_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        munmap(shm_counters_, sizeof(ShmNcclCounters));
        shm_counters_ = nullptr;
    }
    if (shm_fd_ >= 0) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
    if (!shm_name_.empty()) {
        shm_unlink(shm_name_.c_str());
        shm_name_.clear();
    }

    // Unset env vars so they don't bleed into any other child processes.
    unsetenv("NCCL_PROFILER_PLUGIN");
    unsetenv("GATHER_CXI_SHM_PATH");
}

// ---------------------------------------------------------------------------
// discover_nics_from_sysfs()
//
// Enumerates cxi*/hsn* network interfaces from /sys/class/net/, sorts them
// by name (cxi0 < cxi1 < ...), then maps channelId % num_nics → nic_name.
//
// This mapping is empirically confirmed: RCCL assigns channels to NICs in
// round-robin order by NIC index, so channelId % num_nics gives the NIC.
// ---------------------------------------------------------------------------

void NcclMetricSource::discover_nics_from_sysfs() {
    nics_.clear();

    DIR* dir = opendir("/sys/class/net/");
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            std::string name(ent->d_name);
            if (name.size() >= 3 &&
                (name.substr(0, 3) == "cxi" || name.substr(0, 3) == "hsn")) {
                nics_.push_back(name);
            }
        }
        closedir(dir);
    }

    if (nics_.empty()) {
        std::cerr << "[NcclMetricSource] no cxi/hsn interfaces found in "
                     "/sys/class/net/; channel IDs will be used as NIC names\n";
        return;
    }

    std::sort(nics_.begin(), nics_.end());
    std::cerr << "[NcclMetricSource] found " << nics_.size() << " NICs (";
    for (size_t i = 0; i < nics_.size(); i++) {
        if (i) std::cerr << ", ";
        std::cerr << nics_[i];
    }
    std::cerr << ")\n";
}



// ---------------------------------------------------------------------------
// nic_for_channel()  — returns "hsn<N>" or "ch<N>" as fallback
// ---------------------------------------------------------------------------

std::string NcclMetricSource::nic_for_channel(int ch) const {
    if (!nics_.empty()) return nics_[(size_t)ch % nics_.size()];
    return "ch" + std::to_string(ch);
}

