/**
 * metric_source_perf.cpp - Implementation of perf metric source
 */

#include "metric_source_perf.h"
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <chrono>
#include <iostream>

static int perf_event_open(struct perf_event_attr* attr, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    return static_cast<int>(syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags));
}

PerfMetricSource::PerfMetricSource() {
    char hostname_buf[256];
    gethostname(hostname_buf, sizeof(hostname_buf));
    hostname_ = hostname_buf;

    // Define perf events (includes unsupported placeholders for NA columns)
    configs_ = {
        {"cache_misses", "CPU cache misses", "count", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES, false},
        {"context_switches", "Context switches", "count", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES, false},
        {"branch_misses", "Branch mispredictions", "count", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES, false},
        {"cpu_migrations", "CPU migrations", "count", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS, false},
        {"page_faults", "Page faults", "count", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS, false},
        {"dTLB_load_misses", "dTLB load misses", "count", PERF_TYPE_HW_CACHE,
            PERF_COUNT_HW_CACHE_DTLB | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16), false},
        {"iTLB_load_misses", "iTLB load misses", "count", PERF_TYPE_HW_CACHE,
            PERF_COUNT_HW_CACHE_ITLB | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16), false},
        {"emulation_faults", "Emulation faults", "count", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_EMULATION_FAULTS, false},
        {"L1_icache_load_misses", "L1 icache load misses", "count", PERF_TYPE_HW_CACHE,
            PERF_COUNT_HW_CACHE_L1I | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16), false},
        {"L1_dcache_load_misses", "L1 dcache load misses", "count", PERF_TYPE_HW_CACHE,
            PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16), false},
        {"cpu_clock", "CPU clock", "cycles", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK, false},
        {"cycles", "CPU cycles", "cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, false},
        // Unsupported placeholders (expected NA)
        {"LLC-load-misses", "Last-level cache load misses", "count", PERF_TYPE_HW_CACHE,
            PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16), false},
        {"node-load-misses", "Node load misses", "count", PERF_TYPE_HW_CACHE, 0, true}
    };

    for (const auto& cfg : configs_) {
        metadata_.push_back({cfg.name, cfg.description, cfg.unit, true, MetricScope::PER_HOST});
    }
}

MetricScope PerfMetricSource::scope() const {
    return MetricScope::PER_HOST;
}

const std::vector<MetricMetadata>& PerfMetricSource::metadata() const {
    return metadata_;
}

bool PerfMetricSource::init() {
    fds_.clear();
    unsupported_.clear();

    bool any_success = false;

    for (const auto& cfg : configs_) {
        if (cfg.always_unsupported) {
            fds_.push_back(-1);
            unsupported_.insert(cfg.name);
            continue;
        }

        int fd = open_event(cfg);
        if (fd < 0) {
            fds_.push_back(-1);
            unsupported_.insert(cfg.name);
        } else {
            fds_.push_back(fd);
            any_success = true;
        }
    }

    return any_success;
}

void PerfMetricSource::cleanup() {
    for (int fd : fds_) {
        if (fd >= 0) {
            close(fd);
        }
    }
    fds_.clear();
}

MetricSourceSample PerfMetricSource::read() {
    MetricSourceSample sample;
    sample.scope = MetricScope::PER_HOST;

    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    sample.timestamp_ms = static_cast<uint64_t>(std::chrono::duration<double>(duration).count() * 1000.0);

    auto& host_metrics = sample.entities[hostname_];

    for (size_t i = 0; i < configs_.size(); ++i) {
        uint64_t value = UINT64_MAX;
        if (i < fds_.size() && fds_[i] >= 0) {
            value = read_event_value(fds_[i]);
        }
        host_metrics[configs_[i].name] = value;
    }

    return sample;
}

const std::set<std::string>& PerfMetricSource::unsupported_events() const {
    return unsupported_;
}

int PerfMetricSource::open_event(const PerfEventConfig& cfg) {
    struct perf_event_attr pe;
    std::memset(&pe, 0, sizeof(pe));

    pe.type = cfg.type;
    pe.size = sizeof(struct perf_event_attr);
    pe.config = cfg.config;
    pe.disabled = 0;
    pe.inherit = 1;
    pe.exclude_kernel = 1;
    pe.exclude_hv = 1;

    int fd = perf_event_open(&pe, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT || errno == EOPNOTSUPP || errno == EINVAL) {
            return -1;
        }
        std::cerr << "WARN: perf_event_open failed for " << cfg.name
                  << ": " << std::strerror(errno) << std::endl;
        return -1;
    }

    return fd;
}

uint64_t PerfMetricSource::read_event_value(int fd) const {
    uint64_t value = 0;
    ssize_t bytes = ::read(fd, &value, sizeof(value));
    if (bytes != static_cast<ssize_t>(sizeof(value))) {
        return UINT64_MAX;
    }
    return value;
}
