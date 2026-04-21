#include "zmq_transfer.h"
#include "counter_collection.h"
#include "utils.h"
#include "zmq.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <unistd.h>

#define ZMQ_PORT "5555"

std::vector<char> serialize(const AggregatedStats& agg) {
    std::vector<char> buffer;
    size_t count = agg.all_initial.size();
    buffer.insert(buffer.end(), reinterpret_cast<const char*>(&count), reinterpret_cast<const char*>(&count) + sizeof(count));
    for (size_t i = 0; i < count; ++i) {
        size_t hsize = agg.hostnames[i].size();
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&hsize), reinterpret_cast<const char*>(&hsize) + sizeof(hsize));
        buffer.insert(buffer.end(), agg.hostnames[i].begin(), agg.hostnames[i].end());
        long procid = agg.procids[i];
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&procid), reinterpret_cast<const char*>(&procid) + sizeof(procid));
        for (auto v : agg.all_initial[i]) {
            buffer.insert(buffer.end(), reinterpret_cast<const char*>(&v), reinterpret_cast<const char*>(&v) + sizeof(v));
        }
        for (auto v : agg.all_final[i]) {
            buffer.insert(buffer.end(), reinterpret_cast<const char*>(&v), reinterpret_cast<const char*>(&v) + sizeof(v));
        }
    }
    for (auto t : agg.times) {
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&t), reinterpret_cast<const char*>(&t) + sizeof(t));
    }
    // Serialize time_series: number of nodes with time_series
    size_t ts_count = agg.time_series.size();
    buffer.insert(buffer.end(), reinterpret_cast<const char*>(&ts_count), reinterpret_cast<const char*>(&ts_count) + sizeof(ts_count));
    for (const auto& nts : agg.time_series) {
        // hostname
        size_t hsize = nts.hostname.size();
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&hsize), reinterpret_cast<const char*>(&hsize) + sizeof(hsize));
        buffer.insert(buffer.end(), nts.hostname.begin(), nts.hostname.end());
        // procid and execution_time
        long procid = nts.procid;
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&procid), reinterpret_cast<const char*>(&procid) + sizeof(procid));
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&nts.execution_time), reinterpret_cast<const char*>(&nts.execution_time) + sizeof(nts.execution_time));
        // number of samples
        size_t sample_count = nts.samples.size();
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&sample_count), reinterpret_cast<const char*>(&sample_count) + sizeof(sample_count));
        for (const auto& s : nts.samples) {
            // timestamp
            buffer.insert(buffer.end(), reinterpret_cast<const char*>(&s.timestamp), reinterpret_cast<const char*>(&s.timestamp) + sizeof(s.timestamp));
            // number of nics
            size_t nic_count = s.nic_counters.size();
            buffer.insert(buffer.end(), reinterpret_cast<const char*>(&nic_count), reinterpret_cast<const char*>(&nic_count) + sizeof(nic_count));
            for (const auto& kv : s.nic_counters) {
                // nic name
                size_t nsize = kv.first.size();
                buffer.insert(buffer.end(), reinterpret_cast<const char*>(&nsize), reinterpret_cast<const char*>(&nsize) + sizeof(nsize));
                buffer.insert(buffer.end(), kv.first.begin(), kv.first.end());
                // counter values
                for (auto v : kv.second) {
                    buffer.insert(buffer.end(), reinterpret_cast<const char*>(&v), reinterpret_cast<const char*>(&v) + sizeof(v));
                }
            }
            // NEW: Serialize sources map (plugin architecture)
            size_t source_count = s.sources.size();
            buffer.insert(buffer.end(), reinterpret_cast<const char*>(&source_count), reinterpret_cast<const char*>(&source_count) + sizeof(source_count));
            for (const auto& [source_name, entity_map] : s.sources) {
                // source name
                size_t sname_len = source_name.size();
                buffer.insert(buffer.end(), reinterpret_cast<const char*>(&sname_len), reinterpret_cast<const char*>(&sname_len) + sizeof(sname_len));
                buffer.insert(buffer.end(), source_name.begin(), source_name.end());
                // number of entities (e.g., "hsn0", "hsn1", or "host")
                size_t entity_count = entity_map.size();
                buffer.insert(buffer.end(), reinterpret_cast<const char*>(&entity_count), reinterpret_cast<const char*>(&entity_count) + sizeof(entity_count));
                for (const auto& [entity_id, metrics] : entity_map) {
                    // entity name
                    size_t eid_len = entity_id.size();
                    buffer.insert(buffer.end(), reinterpret_cast<const char*>(&eid_len), reinterpret_cast<const char*>(&eid_len) + sizeof(eid_len));
                    buffer.insert(buffer.end(), entity_id.begin(), entity_id.end());
                    // metric values
                    size_t metric_count = metrics.size();
                    buffer.insert(buffer.end(), reinterpret_cast<const char*>(&metric_count), reinterpret_cast<const char*>(&metric_count) + sizeof(metric_count));
                    for (const auto& [metric_name, value] : metrics) {
                        // metric name
                        size_t mname_len = metric_name.size();
                        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&mname_len), reinterpret_cast<const char*>(&mname_len) + sizeof(mname_len));
                        buffer.insert(buffer.end(), metric_name.begin(), metric_name.end());
                        // metric value
                        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&value), reinterpret_cast<const char*>(&value) + sizeof(value));
                    }
                }
            }
        }
        // Serialize latency_timestamps and repeat_numbers
        size_t lat_count = nts.latency_timestamps.size();
        if (is_logging_enabled()) {
            std::cerr << "[SERIALIZE] Node " << nts.hostname << " has " << lat_count << " latency timestamps" << std::endl;
        }
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&lat_count), reinterpret_cast<const char*>(&lat_count) + sizeof(lat_count));
        for (const auto& lat_pair : nts.latency_timestamps) {
            double ts = lat_pair.first;
            double lat = lat_pair.second;
            buffer.insert(buffer.end(), reinterpret_cast<const char*>(&ts), reinterpret_cast<const char*>(&ts) + sizeof(ts));
            buffer.insert(buffer.end(), reinterpret_cast<const char*>(&lat), reinterpret_cast<const char*>(&lat) + sizeof(lat));
        }
        size_t rep_count = nts.repeat_numbers.size();
        buffer.insert(buffer.end(), reinterpret_cast<const char*>(&rep_count), reinterpret_cast<const char*>(&rep_count) + sizeof(rep_count));
        for (const auto& rep : nts.repeat_numbers) {
            buffer.insert(buffer.end(), reinterpret_cast<const char*>(&rep), reinterpret_cast<const char*>(&rep) + sizeof(rep));
        }
    }
    return buffer;
}

AggregatedStats deserialize(const std::vector<char>& data) {
    AggregatedStats agg;
    size_t pos = 0;
    size_t count;
    memcpy(&count, &data[pos], sizeof(count));
    pos += sizeof(count);
    for (size_t i = 0; i < count; ++i) {
        size_t hsize;
        memcpy(&hsize, &data[pos], sizeof(hsize));
        pos += sizeof(hsize);
        std::string hname(&data[pos], hsize);
        pos += hsize;
        long procid;
        memcpy(&procid, &data[pos], sizeof(procid));
        pos += sizeof(procid);
        agg.hostnames.push_back(hname);
        agg.procids.push_back(procid);
        std::vector<uint64_t> vals_initial;
        for (size_t j = 0; j < counter_names.size(); ++j) {
            uint64_t v;
            memcpy(&v, &data[pos], sizeof(v));
            pos += sizeof(v);
            vals_initial.push_back(v);
        }
        agg.all_initial.push_back(vals_initial);
        std::vector<uint64_t> vals_final;
        for (size_t j = 0; j < counter_names.size(); ++j) {
            uint64_t v;
            memcpy(&v, &data[pos], sizeof(v));
            pos += sizeof(v);
            vals_final.push_back(v);
        }
        agg.all_final.push_back(vals_final);
    }
    for (size_t i = 0; i < count; ++i) {
        double t;
        memcpy(&t, &data[pos], sizeof(t));
        pos += sizeof(t);
        agg.times.push_back(t);
    }
    // Deserialize time_series
    size_t ts_count = 0;
    if (pos + sizeof(ts_count) <= data.size()) {
        memcpy(&ts_count, &data[pos], sizeof(ts_count));
        pos += sizeof(ts_count);
        for (size_t i = 0; i < ts_count; ++i) {
            NodeTimeSeries nts;
            size_t hsize;
            memcpy(&hsize, &data[pos], sizeof(hsize));
            pos += sizeof(hsize);
            nts.hostname = std::string(&data[pos], hsize);
            pos += hsize;
            memcpy(&nts.procid, &data[pos], sizeof(nts.procid));
            pos += sizeof(nts.procid);
            memcpy(&nts.execution_time, &data[pos], sizeof(nts.execution_time));
            pos += sizeof(nts.execution_time);
            size_t sample_count = 0;
            memcpy(&sample_count, &data[pos], sizeof(sample_count));
            pos += sizeof(sample_count);
            for (size_t s = 0; s < sample_count; ++s) {
                TimeSample ts;
                memcpy(&ts.timestamp, &data[pos], sizeof(ts.timestamp));
                pos += sizeof(ts.timestamp);
                size_t nic_count = 0;
                memcpy(&nic_count, &data[pos], sizeof(nic_count));
                pos += sizeof(nic_count);
                for (size_t n = 0; n < nic_count; ++n) {
                    size_t nsize = 0;
                    memcpy(&nsize, &data[pos], sizeof(nsize));
                    pos += sizeof(nsize);
                    std::string nicname(&data[pos], nsize);
                    pos += nsize;
                    std::vector<uint64_t> vals;
                    for (size_t c = 0; c < counter_names.size(); ++c) {
                        uint64_t v;
                        memcpy(&v, &data[pos], sizeof(v));
                        pos += sizeof(v);
                        vals.push_back(v);
                    }
                    ts.nic_counters[nicname] = vals;
                }
                // NEW: Deserialize sources map
                size_t source_count = 0;
                if (pos + sizeof(source_count) <= data.size()) {
                    memcpy(&source_count, &data[pos], sizeof(source_count));
                    pos += sizeof(source_count);
                    for (size_t src = 0; src < source_count; ++src) {
                        // source name
                        size_t sname_len = 0;
                        if (pos + sizeof(sname_len) <= data.size()) {
                            memcpy(&sname_len, &data[pos], sizeof(sname_len));
                            pos += sizeof(sname_len);
                            std::string source_name(&data[pos], sname_len);
                            pos += sname_len;
                            // number of entities
                            size_t entity_count = 0;
                            memcpy(&entity_count, &data[pos], sizeof(entity_count));
                            pos += sizeof(entity_count);
                            for (size_t ent = 0; ent < entity_count; ++ent) {
                                // entity id
                                size_t eid_len = 0;
                                memcpy(&eid_len, &data[pos], sizeof(eid_len));
                                pos += sizeof(eid_len);
                                std::string entity_id(&data[pos], eid_len);
                                pos += eid_len;
                                // metric values
                                size_t metric_count = 0;
                                memcpy(&metric_count, &data[pos], sizeof(metric_count));
                                pos += sizeof(metric_count);
                                for (size_t met = 0; met < metric_count; ++met) {
                                    // metric name
                                    size_t mname_len = 0;
                                    memcpy(&mname_len, &data[pos], sizeof(mname_len));
                                    pos += sizeof(mname_len);
                                    std::string metric_name(&data[pos], mname_len);
                                    pos += mname_len;
                                    // metric value
                                    uint64_t value = 0;
                                    memcpy(&value, &data[pos], sizeof(value));
                                    pos += sizeof(value);
                                    ts.sources[source_name][entity_id][metric_name] = value;
                                }
                            }
                        }
                    }
                }
                nts.samples.push_back(ts);
            }
            // Deserialize latency_timestamps and repeat_numbers
            size_t lat_count = 0;
            if (pos + sizeof(lat_count) <= data.size()) {
                memcpy(&lat_count, &data[pos], sizeof(lat_count));
                if (is_logging_enabled()) {
                    std::cerr << "[DESERIALIZE] Node " << nts.hostname << " has " << lat_count << " latency timestamps" << std::endl;
                }
                pos += sizeof(lat_count);
                for (size_t l = 0; l < lat_count; ++l) {
                    double ts = 0.0, lat = 0.0;
                    if (pos + sizeof(ts) + sizeof(lat) <= data.size()) {
                        memcpy(&ts, &data[pos], sizeof(ts));
                        pos += sizeof(ts);
                        memcpy(&lat, &data[pos], sizeof(lat));
                        pos += sizeof(lat);
                        nts.latency_timestamps.push_back({ts, lat});
                    }
                }
            }
            size_t rep_count = 0;
            if (pos + sizeof(rep_count) <= data.size()) {
                memcpy(&rep_count, &data[pos], sizeof(rep_count));
                pos += sizeof(rep_count);
                for (size_t r = 0; r < rep_count; ++r) {
                    int rep = 0;
                    if (pos + sizeof(rep) <= data.size()) {
                        memcpy(&rep, &data[pos], sizeof(rep));
                        pos += sizeof(rep);
                        nts.repeat_numbers.push_back(rep);
                    }
                }
            }
            agg.time_series.push_back(nts);
        }
    }
    return agg;
}

void receive_aggregated_stats(AggregatedStats& agg) {
    zmq::context_t context(1);
    zmq::socket_t server(context, ZMQ_PULL);
    server.bind("tcp://*:" ZMQ_PORT);

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    if (is_logging_enabled()) {
        std::cout << "[RECEIVE] Node " << hostname << " waiting for data..." << std::endl;
    }

    zmq::message_t request;
    (void) server.recv(request, zmq::recv_flags::none);
    std::vector<char> data(static_cast<char*>(request.data()), static_cast<char*>(request.data()) + request.size());
    if (is_logging_enabled()) {
        std::cout << "[RECEIVE] Node " << hostname << " received " << data.size() << " bytes of data." << std::endl;
    }
    agg = deserialize(data);

    server.close();
    context.close();
}

void send_to_previous(const AggregatedStats& agg, long procid) {
    zmq::context_t context(1);
    zmq::socket_t client(context, ZMQ_PUSH);
    std::vector<std::string> nodes = get_node_list_from_scontrol();
    std::string prev_node = get_previous_node(nodes, procid);
    std::string endpoint = "tcp://" + prev_node + ":" ZMQ_PORT;
    client.connect(endpoint.c_str());
    std::vector<char> data = serialize(agg);

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    if (is_logging_enabled()) {
        std::cout << "[SEND] Node " << hostname << " sending " << data.size() << " bytes to " << prev_node << std::endl;
    }
    zmq::message_t msg(data.data(), data.size());
    client.send(msg, zmq::send_flags::none);

    client.close();
    context.close();
}
