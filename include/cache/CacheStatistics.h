#pragma once

#include <cstdint>
#include <chrono>

namespace opcua2http {

/**
 * @brief Enhanced cache statistics structure for comprehensive monitoring
 *
 * This structure provides detailed performance metrics, timing information,
 * and cache health indicators for monitoring and optimization purposes.
 *
 * Requirements: 6.1, 6.2, 6.3
 */
struct CacheStatistics {
    // Performance metrics
    uint64_t totalRequests{0};          // Total cache requests (hits + misses)
    uint64_t cacheHits{0};              // Total cache hits (all types)
    uint64_t cacheMisses{0};            // Total cache misses
    uint64_t freshHits{0};              // Cache hits within refresh threshold (< 3s)
    uint64_t staleRefreshes{0};         // Stale cache hits requiring background refresh (3-10s)
    uint64_t expiredReads{0};           // Expired cache reads requiring synchronous reload (> 10s)
    uint64_t batchOperations{0};        // Number of batch operations performed
    uint64_t concurrentReadBlocks{0};   // Number of times concurrent reads were blocked

    // Timing metrics (in milliseconds)
    double averageResponseTime{0.0};    // Average overall response time
    double cacheHitResponseTime{0.0};   // Average response time for cache hits
    double cacheMissResponseTime{0.0};  // Average response time for cache misses
    double freshHitResponseTime{0.0};   // Average response time for fresh cache hits
    double staleHitResponseTime{0.0};   // Average response time for stale cache hits
    double expiredReadResponseTime{0.0}; // Average response time for expired reads

    // Cache health metrics
    size_t totalEntries{0};             // Total number of cache entries
    size_t freshEntries{0};             // Number of fresh entries (< 3s)
    size_t staleEntries{0};             // Number of stale entries (3-10s)
    size_t expiredEntries{0};           // Number of expired entries (> 10s)
    size_t subscribedEntries{0};        // Number of entries with active subscriptions

    // Cache efficiency metrics
    double hitRatio{0.0};               // Overall cache hit ratio (hits / total_requests)
    double freshHitRatio{0.0};          // Fresh hit ratio (fresh_hits / total_hits)
    double staleHitRatio{0.0};          // Stale hit ratio (stale_hits / total_hits)
    double expiredReadRatio{0.0};       // Expired read ratio (expired_reads / total_hits)

    // Memory metrics
    size_t memoryUsageBytes{0};         // Estimated memory usage in bytes
    size_t memoryUsageMB{0};            // Estimated memory usage in megabytes
    double memoryUsageRatio{0.0};       // Memory usage ratio (used / max)

    // Operational metrics
    uint64_t totalReads{0};             // Total read operations
    uint64_t totalWrites{0};            // Total write operations
    uint64_t totalCleanups{0};          // Total cleanup operations performed
    uint64_t entriesRemoved{0};         // Total entries removed by cleanup

    // Timestamps
    std::chrono::steady_clock::time_point creationTime;  // Cache creation time
    std::chrono::steady_clock::time_point lastCleanup;   // Last cleanup time
    std::chrono::steady_clock::time_point lastUpdate;    // Last statistics update time

    /**
     * @brief Calculate derived metrics from raw counters
     *
     * This method computes ratios and percentages from the raw counter values.
     * Should be called after updating raw counters to ensure consistency.
     */
    void calculateDerivedMetrics() {
        // Calculate hit ratio
        if (totalRequests > 0) {
            hitRatio = static_cast<double>(cacheHits) / totalRequests;
        } else {
            hitRatio = 0.0;
        }

        // Calculate hit type ratios
        if (cacheHits > 0) {
            freshHitRatio = static_cast<double>(freshHits) / cacheHits;
            staleHitRatio = static_cast<double>(staleRefreshes) / cacheHits;
            expiredReadRatio = static_cast<double>(expiredReads) / cacheHits;
        } else {
            freshHitRatio = 0.0;
            staleHitRatio = 0.0;
            expiredReadRatio = 0.0;
        }

        // Calculate memory usage in MB
        memoryUsageMB = memoryUsageBytes / (1024 * 1024);

        // Update last update timestamp
        lastUpdate = std::chrono::steady_clock::now();
    }

    /**
     * @brief Get cache efficiency score (0.0 to 1.0)
     *
     * Efficiency score considers hit ratio and freshness of cache entries.
     * Higher score indicates better cache performance.
     *
     * @return Efficiency score between 0.0 (poor) and 1.0 (excellent)
     */
    double getCacheEfficiency() const {
        // Weight factors: hit ratio (60%), fresh hit ratio (30%), low expired ratio (10%)
        double hitScore = hitRatio * 0.6;
        double freshnessScore = freshHitRatio * 0.3;
        double expirationScore = (1.0 - expiredReadRatio) * 0.1;

        return hitScore + freshnessScore + expirationScore;
    }

    /**
     * @brief Check if cache is healthy based on key metrics
     *
     * A cache is considered healthy if:
     * - Hit ratio is above 70%
     * - Fresh hit ratio is above 50%
     * - Expired read ratio is below 20%
     *
     * Note: Returns true if there are insufficient requests (< 10) to make
     * a meaningful health assessment, avoiding false alarms during startup.
     *
     * @return True if cache is healthy, false otherwise
     */
    bool isHealthy() const {
        // Need at least 10 requests to make a meaningful health assessment
        // This avoids false "degraded" status during system startup
        if (totalRequests < 10) {
            return true;
        }

        return hitRatio >= 0.7 &&
               freshHitRatio >= 0.5 &&
               expiredReadRatio <= 0.2;
    }

    /**
     * @brief Get average cache entry age in seconds
     *
     * Estimates average age based on distribution of fresh/stale/expired entries.
     *
     * @return Estimated average age in seconds
     */
    double getAverageAge() const {
        if (totalEntries == 0) {
            return 0.0;
        }

        // Estimate: fresh ~1.5s, stale ~6.5s, expired ~15s
        double estimatedTotalAge = (freshEntries * 1.5) +
                                   (staleEntries * 6.5) +
                                   (expiredEntries * 15.0);

        return estimatedTotalAge / totalEntries;
    }

    /**
     * @brief Reset all statistics counters to zero
     */
    void reset() {
        totalRequests = 0;
        cacheHits = 0;
        cacheMisses = 0;
        freshHits = 0;
        staleRefreshes = 0;
        expiredReads = 0;
        batchOperations = 0;
        concurrentReadBlocks = 0;

        averageResponseTime = 0.0;
        cacheHitResponseTime = 0.0;
        cacheMissResponseTime = 0.0;
        freshHitResponseTime = 0.0;
        staleHitResponseTime = 0.0;
        expiredReadResponseTime = 0.0;

        totalEntries = 0;
        freshEntries = 0;
        staleEntries = 0;
        expiredEntries = 0;
        subscribedEntries = 0;

        hitRatio = 0.0;
        freshHitRatio = 0.0;
        staleHitRatio = 0.0;
        expiredReadRatio = 0.0;

        memoryUsageBytes = 0;
        memoryUsageMB = 0;
        memoryUsageRatio = 0.0;

        totalReads = 0;
        totalWrites = 0;
        totalCleanups = 0;
        entriesRemoved = 0;

        lastUpdate = std::chrono::steady_clock::now();
    }
};

} // namespace opcua2http
