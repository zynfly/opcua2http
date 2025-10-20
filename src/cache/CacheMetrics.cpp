#include "cache/CacheMetrics.h"
#include "cache/CacheManager.h"
#include "core/BackgroundUpdater.h"
#include <spdlog/spdlog.h>
#include <iomanip>
#include <sstream>

namespace opcua2http {

CacheMetrics::CacheMetrics(CacheManager* cacheManager, BackgroundUpdater* backgroundUpdater)
    : cacheManager_(cacheManager)
    , backgroundUpdater_(backgroundUpdater)
    , creationTime_(std::chrono::steady_clock::now())
{
    if (!cacheManager_) {
        throw std::invalid_argument("CacheManager cannot be null");
    }

    lastUpdate_.store(creationTime_);

    spdlog::debug("CacheMetrics initialized");
}

void CacheMetrics::recordCacheHit(const std::string& /* nodeId */, double responseTimeMs) {
    totalRequests_.fetch_add(1, std::memory_order_relaxed);
    cacheHits_.fetch_add(1, std::memory_order_relaxed);

    if (responseTimeMs > 0.0) {
        std::lock_guard<std::mutex> lock(timingMutex_);
        updateAverageTime(totalResponseTime_, hitResponseCount_, responseTimeMs);
        updateAverageTime(totalHitResponseTime_, hitResponseCount_, responseTimeMs);
    }

    lastUpdate_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
}

void CacheMetrics::recordCacheMiss(const std::string& /* nodeId */, double responseTimeMs) {
    totalRequests_.fetch_add(1, std::memory_order_relaxed);
    cacheMisses_.fetch_add(1, std::memory_order_relaxed);

    if (responseTimeMs > 0.0) {
        std::lock_guard<std::mutex> lock(timingMutex_);
        updateAverageTime(totalResponseTime_, missResponseCount_, responseTimeMs);
        updateAverageTime(totalMissResponseTime_, missResponseCount_, responseTimeMs);
    }

    lastUpdate_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
}

void CacheMetrics::recordStaleRefresh(const std::string& /* nodeId */, double responseTimeMs) {
    staleRefreshes_.fetch_add(1, std::memory_order_relaxed);

    if (responseTimeMs > 0.0) {
        std::lock_guard<std::mutex> lock(timingMutex_);
        updateAverageTime(totalStaleHitResponseTime_, staleHitResponseCount_, responseTimeMs);
    }

    lastUpdate_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
}

void CacheMetrics::recordExpiredRead(const std::string& /* nodeId */, double responseTimeMs) {
    expiredReads_.fetch_add(1, std::memory_order_relaxed);

    if (responseTimeMs > 0.0) {
        std::lock_guard<std::mutex> lock(timingMutex_);
        updateAverageTime(totalExpiredReadResponseTime_, expiredReadResponseCount_, responseTimeMs);
    }

    lastUpdate_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
}

void CacheMetrics::recordFreshHit(const std::string& /* nodeId */, double responseTimeMs) {
    freshHits_.fetch_add(1, std::memory_order_relaxed);

    if (responseTimeMs > 0.0) {
        std::lock_guard<std::mutex> lock(timingMutex_);
        updateAverageTime(totalFreshHitResponseTime_, freshHitResponseCount_, responseTimeMs);
    }

    lastUpdate_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
}

void CacheMetrics::recordBatchOperation(size_t /* batchSize */) {
    batchOperations_.fetch_add(1, std::memory_order_relaxed);
    lastUpdate_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
}

void CacheMetrics::recordConcurrentReadBlock(const std::string& /* nodeId */) {
    concurrentReadBlocks_.fetch_add(1, std::memory_order_relaxed);
    lastUpdate_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
}

void CacheMetrics::recordCleanup(size_t entriesRemoved) {
    totalCleanups_.fetch_add(1, std::memory_order_relaxed);
    entriesRemoved_.fetch_add(entriesRemoved, std::memory_order_relaxed);
    lastUpdate_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
}

CacheStatistics CacheMetrics::getStatistics() const {
    CacheStatistics stats;

    // Performance metrics
    stats.totalRequests = totalRequests_.load(std::memory_order_relaxed);
    stats.cacheHits = cacheHits_.load(std::memory_order_relaxed);
    stats.cacheMisses = cacheMisses_.load(std::memory_order_relaxed);
    stats.freshHits = freshHits_.load(std::memory_order_relaxed);
    stats.staleRefreshes = staleRefreshes_.load(std::memory_order_relaxed);
    stats.expiredReads = expiredReads_.load(std::memory_order_relaxed);
    stats.batchOperations = batchOperations_.load(std::memory_order_relaxed);
    stats.concurrentReadBlocks = concurrentReadBlocks_.load(std::memory_order_relaxed);

    // Timing metrics
    {
        std::lock_guard<std::mutex> lock(timingMutex_);

        stats.averageResponseTime = hitResponseCount_ + missResponseCount_ > 0 ?
            totalResponseTime_ / (hitResponseCount_ + missResponseCount_) : 0.0;

        stats.cacheHitResponseTime = hitResponseCount_ > 0 ?
            totalHitResponseTime_ / hitResponseCount_ : 0.0;

        stats.cacheMissResponseTime = missResponseCount_ > 0 ?
            totalMissResponseTime_ / missResponseCount_ : 0.0;

        stats.freshHitResponseTime = freshHitResponseCount_ > 0 ?
            totalFreshHitResponseTime_ / freshHitResponseCount_ : 0.0;

        stats.staleHitResponseTime = staleHitResponseCount_ > 0 ?
            totalStaleHitResponseTime_ / staleHitResponseCount_ : 0.0;

        stats.expiredReadResponseTime = expiredReadResponseCount_ > 0 ?
            totalExpiredReadResponseTime_ / expiredReadResponseCount_ : 0.0;
    }

    // Get cache health metrics from cache manager
    size_t freshCount = 0, staleCount = 0, expiredCount = 0;
    getCacheHealthMetrics(freshCount, staleCount, expiredCount);

    stats.totalEntries = cacheManager_->size();
    stats.freshEntries = freshCount;
    stats.staleEntries = staleCount;
    stats.expiredEntries = expiredCount;

    // Get cache manager stats for additional metrics
    auto cacheStats = cacheManager_->getStats();
    stats.subscribedEntries = cacheStats.subscribedEntries;
    stats.memoryUsageBytes = cacheStats.memoryUsageBytes;
    stats.totalReads = cacheStats.totalReads;
    stats.totalWrites = cacheStats.totalWrites;

    // Operational metrics
    stats.totalCleanups = totalCleanups_.load(std::memory_order_relaxed);
    stats.entriesRemoved = entriesRemoved_.load(std::memory_order_relaxed);

    // Timestamps
    stats.creationTime = creationTime_;
    stats.lastCleanup = cacheStats.lastCleanup;
    stats.lastUpdate = lastUpdate_.load(std::memory_order_relaxed);

    // Calculate derived metrics
    stats.calculateDerivedMetrics();

    return stats;
}

nlohmann::json CacheMetrics::getMetricsJSON(bool includeTimestamps) const {
    auto stats = getStatistics();

    nlohmann::json metrics = {
        {"performance", {
            {"total_requests", stats.totalRequests},
            {"cache_hits", stats.cacheHits},
            {"cache_misses", stats.cacheMisses},
            {"fresh_hits", stats.freshHits},
            {"stale_refreshes", stats.staleRefreshes},
            {"expired_reads", stats.expiredReads},
            {"batch_operations", stats.batchOperations},
            {"concurrent_read_blocks", stats.concurrentReadBlocks}
        }},
        {"timing", {
            {"average_response_time_ms", stats.averageResponseTime},
            {"cache_hit_response_time_ms", stats.cacheHitResponseTime},
            {"cache_miss_response_time_ms", stats.cacheMissResponseTime},
            {"fresh_hit_response_time_ms", stats.freshHitResponseTime},
            {"stale_hit_response_time_ms", stats.staleHitResponseTime},
            {"expired_read_response_time_ms", stats.expiredReadResponseTime}
        }},
        {"cache_health", {
            {"total_entries", stats.totalEntries},
            {"fresh_entries", stats.freshEntries},
            {"stale_entries", stats.staleEntries},
            {"expired_entries", stats.expiredEntries},
            {"subscribed_entries", stats.subscribedEntries},
            {"average_age_seconds", stats.getAverageAge()}
        }},
        {"efficiency", {
            {"hit_ratio", stats.hitRatio},
            {"fresh_hit_ratio", stats.freshHitRatio},
            {"stale_hit_ratio", stats.staleHitRatio},
            {"expired_read_ratio", stats.expiredReadRatio},
            {"cache_efficiency_score", stats.getCacheEfficiency()},
            {"is_healthy", stats.isHealthy()}
        }},
        {"memory", {
            {"usage_bytes", stats.memoryUsageBytes},
            {"usage_mb", stats.memoryUsageMB},
            {"usage_ratio", stats.memoryUsageRatio}
        }},
        {"operations", {
            {"total_reads", stats.totalReads},
            {"total_writes", stats.totalWrites},
            {"total_cleanups", stats.totalCleanups},
            {"entries_removed", stats.entriesRemoved}
        }}
    };

    // Add background updater statistics if available
    if (backgroundUpdater_) {
        auto bgStats = backgroundUpdater_->getStats();
        metrics["background_updates"] = {
            {"total_updates", bgStats.totalUpdates},
            {"successful_updates", bgStats.successfulUpdates},
            {"failed_updates", bgStats.failedUpdates},
            {"queued_updates", bgStats.queuedUpdates},
            {"duplicate_updates", bgStats.duplicateUpdates},
            {"average_update_time_ms", bgStats.averageUpdateTime}
        };
    }

    // Add timestamps if requested
    if (includeTimestamps) {
        metrics["timestamps"] = {
            {"uptime_seconds", getUptimeSeconds()},
            {"creation_time", formatTimestamp(stats.creationTime)},
            {"last_cleanup", formatTimestamp(stats.lastCleanup)},
            {"last_update", formatTimestamp(stats.lastUpdate)}
        };
    }

    return metrics;
}

double CacheMetrics::getCacheEfficiency() const {
    auto stats = getStatistics();
    return stats.getCacheEfficiency();
}

bool CacheMetrics::isHealthy() const {
    auto stats = getStatistics();
    return stats.isHealthy();
}

void CacheMetrics::reset() {
    totalRequests_.store(0, std::memory_order_relaxed);
    cacheHits_.store(0, std::memory_order_relaxed);
    cacheMisses_.store(0, std::memory_order_relaxed);
    freshHits_.store(0, std::memory_order_relaxed);
    staleRefreshes_.store(0, std::memory_order_relaxed);
    expiredReads_.store(0, std::memory_order_relaxed);
    batchOperations_.store(0, std::memory_order_relaxed);
    concurrentReadBlocks_.store(0, std::memory_order_relaxed);
    totalCleanups_.store(0, std::memory_order_relaxed);
    entriesRemoved_.store(0, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(timingMutex_);
        totalResponseTime_ = 0.0;
        totalHitResponseTime_ = 0.0;
        totalMissResponseTime_ = 0.0;
        totalFreshHitResponseTime_ = 0.0;
        totalStaleHitResponseTime_ = 0.0;
        totalExpiredReadResponseTime_ = 0.0;
        hitResponseCount_ = 0;
        missResponseCount_ = 0;
        freshHitResponseCount_ = 0;
        staleHitResponseCount_ = 0;
        expiredReadResponseCount_ = 0;
    }

    lastUpdate_.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);

    spdlog::info("Cache metrics reset");
}

void CacheMetrics::setBackgroundUpdater(BackgroundUpdater* backgroundUpdater) {
    backgroundUpdater_ = backgroundUpdater;
}

void CacheMetrics::updateAverageTime(double& totalTime, uint64_t& count, double newTime) {
    // This method assumes the mutex is already locked
    totalTime += newTime;
    count++;
}

void CacheMetrics::getCacheHealthMetrics(size_t& freshCount, size_t& staleCount, size_t& expiredCount) const {
    // Get all cached node IDs and evaluate their status
    auto nodeIds = cacheManager_->getCachedNodeIds();

    freshCount = 0;
    staleCount = 0;
    expiredCount = 0;

    for (const auto& nodeId : nodeIds) {
        auto result = cacheManager_->getCachedValueWithStatus(nodeId);

        if (result.entry.has_value()) {
            switch (result.status) {
                case CacheManager::CacheStatus::FRESH:
                    freshCount++;
                    break;
                case CacheManager::CacheStatus::STALE:
                    staleCount++;
                    break;
                case CacheManager::CacheStatus::EXPIRED:
                    expiredCount++;
                    break;
            }
        }
    }
}

std::string CacheMetrics::formatTimestamp(const std::chrono::steady_clock::time_point& timePoint) const {
    // Convert steady_clock to system_clock for formatting
    auto now = std::chrono::steady_clock::now();
    auto systemNow = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - timePoint);
    auto systemTime = systemNow - diff;

    auto time_t_value = std::chrono::system_clock::to_time_t(systemTime);

#ifdef _WIN32
    std::tm tm_buf;
    gmtime_s(&tm_buf, &time_t_value);
    std::tm* tm = &tm_buf;
#else
    std::tm* tm = std::gmtime(&time_t_value);
#endif

    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

uint64_t CacheMetrics::getUptimeSeconds() const {
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - creationTime_);
    return uptime.count();
}

} // namespace opcua2http
