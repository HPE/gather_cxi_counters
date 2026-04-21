/**
 * metric_registry.cpp - Implementation of MetricRegistry singleton
 */

#include "metric_registry.h"
#include <algorithm>
#include <iostream>

/**
 * Singleton instance getter using Meyer's singleton pattern
 * Thread-safe in C++11 and later (static initialization is synchronized)
 */
MetricRegistry& MetricRegistry::instance() {
    static MetricRegistry _instance;
    return _instance;
}

/**
 * Register a metric source by name
 */
void MetricRegistry::register_source(const std::string& name, std::unique_ptr<MetricSource> source) {
    if (!source) {
        std::cerr << "ERROR: Cannot register null metric source: " << name << std::endl;
        return;
    }
    
    if (sources_.count(name) > 0) {
        std::cerr << "WARNING: Metric source '" << name << "' already registered, replacing" << std::endl;
    }
    
    sources_[name] = std::move(source);
}

/**
 * Look up a registered source by name
 */
MetricSource* MetricRegistry::lookup(const std::string& name) {
    auto it = sources_.find(name);
    if (it != sources_.end()) {
        return it->second.get();
    }
    return nullptr;
}

/**
 * Get list of registered source names
 */
std::vector<std::string> MetricRegistry::source_names() const {
    std::vector<std::string> names;
    for (const auto& [name, source] : sources_) {
        names.push_back(name);
    }
    return names;
}

/**
 * Initialize all registered sources
 * Returns true if all succeeded, false if any failed
 */
bool MetricRegistry::init_all() {
    bool all_success = true;
    
    for (auto& [name, source] : sources_) {
        if (!source->init()) {
            std::cerr << "WARNING: Failed to initialize metric source: " << name << std::endl;
            all_success = false;
        }
    }
    
    return all_success;
}

/**
 * Cleanup all registered sources
 * Calls cleanup() in reverse order of registration
 */
void MetricRegistry::cleanup_all() {
    // Cleanup in reverse order
    auto it = sources_.rbegin();
    while (it != sources_.rend()) {
        if (it->second) {
            it->second->cleanup();
        }
        ++it;
    }
}

/**
 * Get the number of registered sources
 */
size_t MetricRegistry::source_count() const {
    return sources_.size();
}
