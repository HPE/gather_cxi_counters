#ifndef ZMQ_TRANSFER_H
#define ZMQ_TRANSFER_H

#include "counter_aggregation.h"
#include <vector>
#include <string>

// Serialize AggregatedStats to binary
std::vector<char> serialize(const AggregatedStats& agg);

// Deserialize AggregatedStats from binary
AggregatedStats deserialize(const std::vector<char>& data);

// Helper to receive aggregated stats from the next node
void receive_aggregated_stats(AggregatedStats& agg);

// Helper to send aggregated stats to the previous node
void send_to_previous(const AggregatedStats& agg, long procid);

#endif
