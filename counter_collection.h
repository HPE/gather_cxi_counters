#ifndef COUNTER_COLLECTION_H
#define COUNTER_COLLECTION_H

#include <vector>
#include <string>
#include <map>
#include <cstdint>

// Struct to hold counters per NIC
struct NicCounters {
    std::string nic_name;  // e.g., "hsn0", "hsn1"
    std::vector<uint64_t> values;
};

// Full list of all possible counter names (immutable)
extern const std::vector<std::string> all_counter_names;

// Active (filtered) counter names — set by init_counter_filter()
// This is the list used everywhere at runtime.
extern std::vector<std::string> counter_names;

// Initialise counter filtering from GATHER_CXI_COUNTERS env var.
// If the variable is empty or unset the full list is used.
// Must be called once before any read_all_counters*() call.
void init_counter_filter();

// Initialise fd-cached reader.  Opens all sysfs/run files once.
// Must be called after init_counter_filter() and before sampling begins.
void init_counter_fds();

// Close cached file descriptors.
void cleanup_counter_fds();

// Read all counters from telemetry files for the current node (backward compatible)
std::vector<uint64_t> read_all_counters();

// Read counters per NIC, returns map of NIC name to counter values
std::map<std::string, std::vector<uint64_t>> read_all_counters_per_nic();

// Function to subtract two 56-bit integers, handling wraparound
uint64_t subtract_56_bit_integers(uint64_t final, uint64_t initial);

#endif
