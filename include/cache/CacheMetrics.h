#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include "cache/CacheStatistics.h"

namespace opcua2http {

// Forward declaration
class CacheManager;
class BackgroundUpdater;

/**
 * @brief Real-time cache metrics collection system
 *
 * This class provides thread-safe methods for recording cache operations
 * and generating comprehensive statistics for monitoring and API endpoints.
 *
 * Requirements: 6.1, 6.2, 6.3, 6.4
 */
class CacheMetrics {
public:
    /**
     * @brief Constructor
     * @param cacheManager Pointer to cache manager for accessing cache state
     * @param backgroundUpdater Pointer to background updater for update statistics (optional)
     */
    explicit CacheMetrics(CacheManager* cacheManager, BackgroundUpdater* backgroundUpdater = nullptr);

    /**
     * @brief Destructor
     */
    ~CacheMetrics() = default;

    // Disable copy constructor and assignment operator
    CacheMetrics(const CacheMetrics&) = delete;
    CacheMetrics& operator=(const CacheMetrics&) = delete;

    /**
     * @brief Record a cache hit event
     * @param nodeId Node identifier that was hit
     * @param responseTimeMs Response time in milliseconds
     */
    void recordCacheHit(const std::string& nodeId, double responseTimeMs = 0.0);

    /**
     * @brief Record a cache miss event
     * @param nodeId Node identifier that was missed
     * @param responseTimeMs Response time in milliseconds
     */
    void recordCacheMiss(const std::string& nodeId, double responseTimeMs = 0.0);

    /**
     * @brief Record a stale cache refresh event (3-10 seconds)
     * @param nodeId Node identifier that required refresh
     * @param responseTimeMs Response time in milliseconds
     */
    void recordStaleRefresh(const std::string& nodeId, double responseTimeMs = 0.0);

    /**
     * @brief Record an expired cache read event (> 10 seconds)
     * @param nodeId Node identifier that was expired
     * @param responseTimeMs Response time in milliseconds
     */
    void recordExpiredRead(const std::string& nodeId, double responseTimeMs = 0.0);

    /**
     * @brief Record a fresh cache hit event (< 3 seconds)
     * @param nodeId Node identifier that was fresh
     * @param responseTimeMs Response time in milliseconds
     */
    void recordFreshHit(const std::string& nodeId, double responseTimeMs = 0.0);

    /**
     * @brief Record a batch operation
     * @param batchSize Number of items in the batch
     */
    void recordBatchOperation(size_t batchSize);

    /**
     * @brief Record a concurrent read block event
     * @param nodeId Node identifier that caused the block
     */
    void recordConcurrentReadBlock(const std::string& nodeId);

    /**
     * @brief Record a cache cleanup operation
     * @param entriesRemoved Number of entries removed during cleanup
     */
    void recordCleanup(size_t entriesRemoved);

    /**
     * @brief Get current cache statistics
     * @return CacheStatistics structure with current metrics
     */
    CacheStatistics getStatistics() const;

    /**
     * @brief Get metrics as JSON for API endpoints
     * @param includeTimestamps Whether to include timestamp information
     * @return JSON object with formatted metrics
     */
    nlohmann::json getMetricsJSON(bool includeTimestamps = true) const;

    /**
     * @brief Get cache efficiency score (0.0 to 1.0)
     * @return Efficiency score
     */
    double getCacheEfficiency() const;

    /**
     * @brief Check if cache is healthy
     * @return True if cache is healthy, false otherwise
     */
    bool isHealthy() const;

    /**
     * @brief Reset all metrics counters
     */
    void reset();

    /**
     * @brief Set background updater reference for statistics
     * @param backgroundUpdater Pointer to background updater
     */
    void setBackgroundUpdater(BackgroundUpdater* backgroundUpdater);

private:
    // Dependencies
    CacheManager* cacheManager_;
    BackgroundUpdater* backgroundUpdater_;

    // Performance metrics (atomic for thread-safe access)
    mutable std::atomic<uint64_t> totalRequests_{0};
    mutable std::atomic<uint64_t> cacheHits_{0};
    mutable std::atomic<uint64_t> cacheMisses_{0};
    mutable std::atomic<uint64_t> freshHits_{0};
    mutable std::atomic<uint64_t> staleRefreshes_{0};
    mutable std::atomic<uint64_t> expiredReads_{0};
    mutable std::atomic<uint64_t> batchOperations_{0};
    mutable std::atomic<uint64_t> concurrentReadBlocks_{0};
    mutable std::atomic<uint64_t> totalCleanups_{0};
    mutable std::atomic<uint64_t> entriesRemoved_{0};

    // Timing metrics (protected by mutex for complex updates)
    mutable std::mutex timingMutex_;
    double totalResponseTime_{0.0};
    double totalHitResponseTime_{0.0};
    double totalMissResponseTime_{0.0};
    double totalFreshHitResponseTime_{0.0};
    double totalStaleHitResponseTime_{0.0};
    double totalExpiredReadResponseTime_{0.0};
    uint64_t hitResponseCount_{0};
    uint64_t missResponseCount_{0};
    uint64_t freshHitResponseCount_{0};
    uint64_t staleHitResponseCount_{0};
    uint64_t expiredReadResponseCount_{0};

    // Timestamps
    std::chrono::steady_clock::time_point creationTime_;
    mutable std::atomic<std::chrono::steady_clock::time_point> lastUpdate_;

    /**
     * @brief Update average response time for a category
     * @param totalTime Reference to total time accumulator
     * @param count Reference to count accumulator
     * @param newTime New response time to add
     */
    void updateAverageTime(double& totalTime, uint64_t& count, double newTime);

    /**
     * @brief Get cache health metrics from cache manager
     * @param freshCount Output parameter for fresh entries count
     * @param staleCount Output parameter for stale entries count
     * @param expiredCount Output parameter for expired entries count
     */
    void getCacheHealthMetrics(size_t& freshCount, size_t& staleCount, size_t& expiredCount) const;

    /**
     * @brief Format timestamp as ISO 8601 string
     * @param timePoint Time point to format
     * @return ISO 8601 formatted string
     */
    std::string formatTimestamp(const std::chrono::steady_clock::time_point& timePoint) const;

    /**
     * @brief Get uptime in seconds
     * @return Uptime since creation in seconds
     */
    uint64_t getUptimeSeconds() const;
};

} // namespace opcua2http
