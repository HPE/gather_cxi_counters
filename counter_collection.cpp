#include "counter_collection.h"
#include <dirent.h>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>

// ── Full list of all possible counters (immutable) ──────────────────────────
const std::vector<std::string> all_counter_names = {
    "hni_rx_paused_0",
    "hni_rx_paused_1",
    "hni_tx_paused_0",
    "hni_tx_paused_1",
    "pct_no_tct_nacks",
    "pct_trs_rsp_nack_drops",
    "parbs_tarb_pi_posted_pkts",
    "parbs_tarb_pi_posted_blocked_cnt",
    "parbs_tarb_pi_non_posted_blocked_cnt",
    "parbs_tarb_pi_non_posted_pkts",
    "rh:connections_cancelled",
    "rh:nack_no_matching_conn",
    "rh:nack_no_target_conn",
    "rh:nack_no_target_mst",
    "rh:nack_no_target_trs",
    "rh:nack_resource_busy",
    "rh:nacks",
    "rh:nack_sequence_error",
    "rh:pkts_cancelled_o",
    "rh:pkts_cancelled_u",
    "rh:sct_in_use",
    "rh:sct_timeouts",
    "rh:spt_timeouts",
    "rh:spt_timeouts_u",
    "rh:tct_timeouts"
};

// ── Default active counters (only those typically nonzero) ──────────────────
static const std::vector<std::string> default_counter_names = {
    "hni_rx_paused_0",
    "parbs_tarb_pi_posted_pkts",
    "parbs_tarb_pi_posted_blocked_cnt",
    "parbs_tarb_pi_non_posted_blocked_cnt",
    "parbs_tarb_pi_non_posted_pkts"
};

// Active counter list — initialised by init_counter_filter()
std::vector<std::string> counter_names = all_counter_names;  // safe default until init

// ── Counter filtering ───────────────────────────────────────────────────────

void init_counter_filter() {
    const char* env = getenv("GATHER_CXI_COUNTERS");
    if (env && std::string(env) == "all") {
        counter_names = all_counter_names;
        return;
    }
    if (env && env[0] != '\0') {
        // Parse comma-separated list
        std::set<std::string> valid(all_counter_names.begin(), all_counter_names.end());
        std::vector<std::string> filtered;
        std::istringstream ss(env);
        std::string token;
        while (std::getline(ss, token, ',')) {
            // trim whitespace
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            if (valid.count(token)) {
                filtered.push_back(token);
            } else {
                std::cerr << "[counter_filter] WARNING: unknown counter '" << token
                          << "' — skipping" << std::endl;
            }
        }
        if (!filtered.empty()) {
            counter_names = filtered;
        } else {
            std::cerr << "[counter_filter] WARNING: no valid counters in GATHER_CXI_COUNTERS, "
                      << "using defaults" << std::endl;
            counter_names = default_counter_names;
        }
    } else {
        // No env var set → use defaults (nonzero-only)
        counter_names = default_counter_names;
    }
}

// ── FD-cached reader ────────────────────────────────────────────────────────
//
// Instead of open()/read()/close() per counter per interval, we open all
// sysfs and /run/cxi files once at startup and thereafter just lseek()+read().
// This eliminates 2 of 3 syscalls per counter read.

struct CachedCounterFd {
    int fd;           // open file descriptor (-1 if unavailable)
    std::string path; // for diagnostics
};

// Per-NIC, per-counter fd cache.  Indexed [nic_idx][counter_idx].
static std::vector<std::string> cached_devices;
static std::vector<std::vector<CachedCounterFd>> cached_fds;  // [device][counter]
static bool fds_initialised = false;

static std::vector<std::string> discover_hsn_devices() {
    std::vector<std::string> devices;
    DIR* dir = opendir("/sys/class/net");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.substr(0, 3) == "hsn") {
                devices.push_back(name);
            }
        }
        closedir(dir);
    }
    std::sort(devices.begin(), devices.end());
    return devices;
}

static int device_num_from_name(const std::string& device) {
    if (device.size() > 3 && device.substr(0, 3) == "hsn") {
        try { return std::stoi(device.substr(3)); } catch (...) {}
    }
    return 0;
}

static std::string counter_path(const std::string& device, int device_num,
                                const std::string& counter) {
    if (counter.substr(0, 3) == "rh:") {
        return "/run/cxi/cxi" + std::to_string(device_num) + "/" + counter.substr(3);
    }
    return "/sys/class/net/" + device + "/device/telemetry/" + counter;
}

void init_counter_fds() {
    cleanup_counter_fds();
    cached_devices = discover_hsn_devices();
    cached_fds.resize(cached_devices.size());

    for (size_t d = 0; d < cached_devices.size(); ++d) {
        int dev_num = device_num_from_name(cached_devices[d]);
        cached_fds[d].resize(counter_names.size());
        for (size_t c = 0; c < counter_names.size(); ++c) {
            std::string path = counter_path(cached_devices[d], dev_num, counter_names[c]);
            int fd = open(path.c_str(), O_RDONLY);
            cached_fds[d][c] = {fd, path};
        }
    }
    fds_initialised = true;
}

void cleanup_counter_fds() {
    for (auto& dev_fds : cached_fds) {
        for (auto& cf : dev_fds) {
            if (cf.fd >= 0) close(cf.fd);
        }
    }
    cached_fds.clear();
    cached_devices.clear();
    fds_initialised = false;
}

// Read a single counter value from a cached fd.  Returns 0 on failure.
static uint64_t read_counter_fd(int fd) {
    if (fd < 0) return 0;
    // Seek to start and re-read
    if (lseek(fd, 0, SEEK_SET) != 0) return 0;
    char buf[64];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return 0;
    buf[n] = '\0';
    // Parse: value is before '@' (format: "12345@0" in sysfs telemetry)
    char* at = strchr(buf, '@');
    if (at) {
        *at = '\0';
        return strtoull(buf, nullptr, 10);
    }
    // /run/cxi files may just be a plain number
    return strtoull(buf, nullptr, 10);
}

// ── Read functions ──────────────────────────────────────────────────────────

// ── Read functions ──────────────────────────────────────────────────────────

std::map<std::string, std::vector<uint64_t>> read_all_counters_per_nic() {
    std::map<std::string, std::vector<uint64_t>> nic_counters;

    if (fds_initialised) {
        // ── Fast path: cached fds ──
        for (size_t d = 0; d < cached_devices.size(); ++d) {
            std::vector<uint64_t> values(counter_names.size(), 0);
            for (size_t c = 0; c < counter_names.size(); ++c) {
                values[c] = read_counter_fd(cached_fds[d][c].fd);
            }
            nic_counters[cached_devices[d]] = values;
        }
    } else {
        // ── Fallback: open/read/close (used before init_counter_fds) ──
        auto devices = discover_hsn_devices();
        for (const auto& device : devices) {
            std::vector<uint64_t> values(counter_names.size(), 0);
            int dev_num = device_num_from_name(device);
            for (size_t i = 0; i < counter_names.size(); ++i) {
                std::string filepath = counter_path(device, dev_num, counter_names[i]);
                std::ifstream file(filepath);
                if (!file) continue;
                std::string line;
                std::getline(file, line);
                size_t at_pos = line.find('@');
                if (at_pos != std::string::npos) {
                    values[i] = std::stoull(line.substr(0, at_pos));
                }
            }
            nic_counters[device] = values;
        }
    }
    return nic_counters;
}

std::vector<uint64_t> read_all_counters() {
    std::vector<uint64_t> values(counter_names.size(), 0);

    if (fds_initialised) {
        for (size_t d = 0; d < cached_devices.size(); ++d) {
            for (size_t c = 0; c < counter_names.size(); ++c) {
                values[c] += read_counter_fd(cached_fds[d][c].fd);
            }
        }
    } else {
        auto devices = discover_hsn_devices();
        for (const auto& device : devices) {
            int dev_num = device_num_from_name(device);
            for (size_t i = 0; i < counter_names.size(); ++i) {
                std::string filepath = counter_path(device, dev_num, counter_names[i]);
                std::ifstream file(filepath);
                if (!file) continue;
                std::string line;
                std::getline(file, line);
                size_t at_pos = line.find('@');
                if (at_pos != std::string::npos) {
                    values[i] += std::stoull(line.substr(0, at_pos));
                }
            }
        }
    }
    return values;
}

uint64_t subtract_56_bit_integers(uint64_t final, uint64_t initial) {
    const uint64_t MASK_56 = (1ULL << 56) - 1;
    uint64_t f = final & MASK_56;
    uint64_t i = initial & MASK_56;
    if (f >= i) {
        return f - i;
    } else {
        return (1ULL << 56) + f - i;
    }
}
