/**
 * metric_registry.h - Singleton registry for metric sources
 *
 * Manages registration, initialization, and lifecycle of all metric collectors.
 * Acts as the factory and lookup mechanism for metric sources.
 */

#ifndef METRIC_REGISTRY_H
#define METRIC_REGISTRY_H

#include "metric_source.h"
#include <memory>
#include <string>
#include <map>
#include <vector>

/**
 * Singleton registry for all metric sources
 * Thread-safe for initialization, but register_source() should only be called
 * before starting sampling in each process
 */
class MetricRegistry {
public:
    /**
     * Get the singleton instance
     */
    static MetricRegistry& instance();

    /**
     * Register a metric source by name
     * Later sources can be looked up via lookup(name)
     * Should be called during application initialization before init_all()
     */
    void register_source(const std::string& name, std::unique_ptr<MetricSource> source);

    /**
     * Look up a registered source by name
     * Returns nullptr if not found
     */
    MetricSource* lookup(const std::string& name);

    /**
     * Get all registered source names
     */
    std::vector<std::string> source_names() const;

    /**
     * Initialize all registered sources
     * Returns true if all sources initialized successfully
     * If any source fails to init, remaining sources are still attempted
     * Returns false if any source failed
     */
    bool init_all();

    /**
     * Cleanup all registered sources
     * Calls cleanup() on each source in reverse registration order
     */
    void cleanup_all();

    /**
     * Get the number of registered sources
     */
    size_t source_count() const;

private:
    MetricRegistry() = default;
    ~MetricRegistry() = default;

    // Delete copy and move constructors/operators
    MetricRegistry(const MetricRegistry&) = delete;
    MetricRegistry& operator=(const MetricRegistry&) = delete;
    MetricRegistry(MetricRegistry&&) = delete;
    MetricRegistry& operator=(MetricRegistry&&) = delete;

    // Map of source name -> source instance
    std::map<std::string, std::unique_ptr<MetricSource>> sources_;
};

#endif // METRIC_REGISTRY_H
