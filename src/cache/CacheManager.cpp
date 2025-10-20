#include "cache/CacheManager.h"
#include <algorithm>
#include <iostream>
#include <mutex>
#include <vector>

namespace opcua2http {

CacheManager::CacheManager(int cacheExpireMinutes, size_t maxCacheSize,
                          int refreshThresholdSeconds, int expireTimeSeconds)
    : memoryManager_(std::make_unique<CacheMemoryManager>(100 * 1024 * 1024, maxCacheSize))
    , cacheExpireTime_(cacheExpireMinutes)
    , refreshThreshold_(refreshThresholdSeconds)
    , expireTime_(expireTimeSeconds)
    , maxCacheSize_(maxCacheSize)
    , lastCleanup_(std::chrono::steady_clock::now())
    , creationTime_(std::chrono::steady_clock::now()) {

    std::cout << "CacheManager initialized with " << cacheExpireMinutes
              << " minutes expiration, " << refreshThresholdSeconds
              << "s refresh threshold, " << expireTimeSeconds
              << "s expire time, and max size " << maxCacheSize << std::endl;
}

std::optional<CacheManager::CacheEntry> CacheManager::getCachedValue(const std::string& nodeId) {
    // Check access level (lock-free)
    if (!checkAccessLevel(AccessLevel::READ_ONLY)) {
        std::cout << "Access denied: insufficient permissions for read operation" << std::endl;
        return std::nullopt;
    }

    // Lock-free statistics update
    totalReads_.fetch_add(1, std::memory_order_relaxed);

    std::shared_lock<std::shared_mutex> lock(cacheMutex_);

    auto it = cache_.find(nodeId);
    if (it != cache_.end()) {
        // Lock-free last accessed time update
        it->second.updateLastAccessed();
        totalHits_.fetch_add(1, std::memory_order_relaxed);
        return it->second;
    }

    totalMisses_.fetch_add(1, std::memory_order_relaxed);
    return std::nullopt;
}

void CacheManager::updateCache(const std::string& nodeId,
                              const std::string& value,
                              const std::string& status,
                              const std::string& reason,
                              uint64_t timestamp) {
    // Check access level
    if (!checkAccessLevel(AccessLevel::READ_WRITE)) {
        std::cout << "Access denied: insufficient permissions for write operation" << std::endl;
        return;
    }

    std::unique_lock<std::shared_mutex> lock(cacheMutex_);

    totalWrites_.fetch_add(1, std::memory_order_relaxed);

    auto it = cache_.find(nodeId);
    if (it != cache_.end()) {
        // Update existing entry (preserve creationTime)
        it->second.value = value;
        it->second.status = status;
        it->second.reason = reason;
        it->second.timestamp = timestamp;
        it->second.updateLastAccessed(); // Use atomic method

        std::cout << "Cache updated for node " << nodeId << " with value: " << value << std::endl;
    } else {
        // Check memory pressure before adding new entry
        if (memoryManager_->hasMemoryPressure() || memoryManager_->hasEntryPressure()) {
            size_t evicted = handleMemoryPressure();
            std::cout << "Memory pressure detected, evicted " << evicted << " entries" << std::endl;
        }

        // Create new entry
        CacheEntry entry;
        entry.nodeId = nodeId;
        entry.value = value;
        entry.status = status;
        entry.reason = reason;
        entry.timestamp = timestamp;
        entry.creationTime = std::chrono::steady_clock::now();
        entry.lastAccessed.store(std::chrono::steady_clock::now());
        entry.hasSubscription.store(false);

        cache_[nodeId] = entry;
        std::cout << "New cache entry created for node " << nodeId << " with value: " << value << std::endl;

        // Update memory manager (use no-lock version since we already hold the lock)
        memoryManager_->updateCurrentEntryCount(cache_.size());
        memoryManager_->updateCurrentMemoryUsage(getMemoryUsageNoLock());

        // Enforce size limit if necessary
        if (cache_.size() > maxCacheSize_) {
            enforceSizeLimit();
        }
    }
}

void CacheManager::addCacheEntry(const std::string& nodeId, const CacheEntry& entry) {
    // Check memory pressure before acquiring write lock
    bool needsEviction = false;
    if (memoryManager_) {
        needsEviction = memoryManager_->hasMemoryPressure() || memoryManager_->hasEntryPressure();
    }

    std::unique_lock<std::shared_mutex> lock(cacheMutex_);

    // Handle memory pressure if needed
    if (needsEviction) {
        size_t evicted = handleMemoryPressure();
        std::cout << "Memory pressure detected, evicted " << evicted << " entries" << std::endl;
    }

    cache_[nodeId] = entry;
    cache_[nodeId].updateLastAccessed(); // Use atomic method

    std::cout << "Cache entry added for node " << nodeId << std::endl;

    // Update memory manager (use no-lock version since we already hold the lock)
    if (memoryManager_) {
        memoryManager_->updateCurrentEntryCount(cache_.size());
        memoryManager_->updateCurrentMemoryUsage(getMemoryUsageNoLock());
    }

    // Enforce size limit if necessary
    if (cache_.size() > maxCacheSize_) {
        enforceSizeLimit();
    }
}

void CacheManager::addCacheEntry(const ReadResult& result, bool hasSubscription) {
    CacheEntry entry;
    entry.nodeId = result.id;
    entry.value = result.value;
    entry.status = result.success ? "Good" : "Bad";
    entry.reason = result.reason;
    entry.timestamp = result.timestamp;
    entry.creationTime = std::chrono::steady_clock::now();
    entry.lastAccessed.store(std::chrono::steady_clock::now());
    entry.hasSubscription.store(hasSubscription);

    addCacheEntry(result.id, entry);
}

bool CacheManager::removeCacheEntry(const std::string& nodeId) {
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);

    auto it = cache_.find(nodeId);
    if (it != cache_.end()) {
        cache_.erase(it);
        std::cout << "Cache entry removed for node " << nodeId << std::endl;
        return true;
    }

    return false;
}

size_t CacheManager::cleanupExpiredEntries() {
    if (!isAutoCleanupEnabled()) {
        std::cout << "Auto cleanup is disabled, skipping expired entries cleanup" << std::endl;
        return 0;
    }

    std::unique_lock<std::shared_mutex> lock(cacheMutex_);

    size_t removedCount = 0;
    auto now = std::chrono::steady_clock::now();

    for (auto it = cache_.begin(); it != cache_.end();) {
        if (isExpired(it->second)) {
            std::cout << "Removing expired cache entry for node " << it->first << std::endl;
            it = cache_.erase(it);
            ++removedCount;
        } else {
            ++it;
        }
    }

    lastCleanup_ = now;

    if (removedCount > 0) {
        std::cout << "Cleanup removed " << removedCount << " expired cache entries" << std::endl;
    }

    return removedCount;
}

size_t CacheManager::cleanupUnusedEntries() {
    if (!isAutoCleanupEnabled()) {
        std::cout << "Auto cleanup is disabled, skipping unused entries cleanup" << std::endl;
        return 0;
    }

    std::unique_lock<std::shared_mutex> lock(cacheMutex_);

    size_t removedCount = 0;
    auto now = std::chrono::steady_clock::now();
    auto unusedThreshold = now - std::chrono::minutes(30); // Remove entries not accessed in 30 minutes

    for (auto it = cache_.begin(); it != cache_.end();) {
        // Only remove entries without subscriptions that haven't been accessed recently
        if (!it->second.getSubscriptionStatus() && it->second.getLastAccessed() < unusedThreshold) {
            std::cout << "Removing unused cache entry for node " << it->first << std::endl;
            it = cache_.erase(it);
            ++removedCount;
        } else {
            ++it;
        }
    }

    if (removedCount > 0) {
        std::cout << "Cleanup removed " << removedCount << " unused cache entries" << std::endl;
    }

    return removedCount;
}

std::vector<std::string> CacheManager::getCachedNodeIds() const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);

    std::vector<std::string> nodeIds;
    nodeIds.reserve(cache_.size());

    for (const auto& pair : cache_) {
        nodeIds.push_back(pair.first);
    }

    return nodeIds;
}

std::vector<std::string> CacheManager::getSubscribedNodeIds() const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);

    std::vector<std::string> nodeIds;

    for (const auto& pair : cache_) {
        if (pair.second.getSubscriptionStatus()) {
            nodeIds.push_back(pair.first);
        }
    }

    return nodeIds;
}

void CacheManager::setSubscriptionStatus(const std::string& nodeId, bool hasSubscription) {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_); // Use shared lock for atomic operations

    auto it = cache_.find(nodeId);
    if (it != cache_.end()) {
        it->second.setSubscriptionStatus(hasSubscription); // Use atomic method
        it->second.updateLastAccessed(); // Use atomic method

        std::cout << "Subscription status for node " << nodeId
                  << " set to " << (hasSubscription ? "active" : "inactive") << std::endl;
    }
}

CacheManager::CacheStats CacheManager::getStats() const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);

    size_t subscribedCount = 0;
    size_t memoryUsage = 0;

    for (const auto& pair : cache_) {
        if (pair.second.getSubscriptionStatus()) {
            ++subscribedCount;
        }
        memoryUsage += calculateEntrySize(pair.second);
    }

    uint64_t hits = totalHits_.load(std::memory_order_relaxed);
    uint64_t misses = totalMisses_.load(std::memory_order_relaxed);
    double hitRatio = (hits + misses > 0) ? static_cast<double>(hits) / (hits + misses) : 0.0;

    return CacheStats{
        cache_.size(),
        subscribedCount,
        0, // expiredEntries - would need to track this separately
        hits,
        misses,
        totalReads_.load(std::memory_order_relaxed),
        totalWrites_.load(std::memory_order_relaxed),
        memoryUsage,
        hitRatio,
        lastCleanup_,
        creationTime_
    };
}

void CacheManager::clear() {
    // Check access level
    if (!checkAccessLevel(AccessLevel::ADMIN)) {
        std::cout << "Access denied: insufficient permissions for clear operation" << std::endl;
        return;
    }

    std::unique_lock<std::shared_mutex> lock(cacheMutex_);

    size_t count = cache_.size();
    cache_.clear();

    std::cout << "Cache cleared, removed " << count << " entries" << std::endl;
}

size_t CacheManager::size() const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    return cache_.size();
}

bool CacheManager::empty() const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    return cache_.empty();
}

bool CacheManager::isFull() const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    return cache_.size() >= maxCacheSize_;
}

bool CacheManager::isExpired(const CacheEntry& entry) const {
    auto now = std::chrono::steady_clock::now();
    return (now - entry.getLastAccessed()) > cacheExpireTime_;
}

size_t CacheManager::enforceSizeLimit() {
    // This method assumes unique_lock is already held

    if (cache_.size() <= maxCacheSize_) {
        return 0;
    }

    size_t toRemove = cache_.size() - maxCacheSize_;
    size_t removedCount = 0;

    // Create vector of entries sorted by last accessed time (oldest first)
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> entries;
    entries.reserve(cache_.size());

    for (const auto& pair : cache_) {
        // Don't remove entries with active subscriptions
        if (!pair.second.getSubscriptionStatus()) {
            entries.emplace_back(pair.first, pair.second.getLastAccessed());
        }
    }

    // Sort by last accessed time (oldest first)
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) {
                  return a.second < b.second;
              });

    // Remove oldest entries without subscriptions
    for (size_t i = 0; i < std::min(toRemove, entries.size()); ++i) {
        auto it = cache_.find(entries[i].first);
        if (it != cache_.end()) {
            std::cout << "Removing cache entry for node " << it->first
                      << " due to size limit" << std::endl;
            cache_.erase(it);
            ++removedCount;
        }
    }

    return removedCount;
}



size_t CacheManager::getMemoryUsage() const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    return getMemoryUsageNoLock();
}

size_t CacheManager::getMemoryUsageNoLock() const {
    // This method assumes lock is already held
    size_t totalSize = 0;
    for (const auto& pair : cache_) {
        totalSize += calculateEntrySize(pair.second);
    }

    return totalSize;
}

double CacheManager::getHitRatio() const {
    uint64_t hits = totalHits_.load(std::memory_order_relaxed);
    uint64_t misses = totalMisses_.load(std::memory_order_relaxed);

    if (hits + misses == 0) {
        return 0.0;
    }

    return static_cast<double>(hits) / (hits + misses);
}

void CacheManager::setAccessLevel(AccessLevel level) {
    accessLevel_.store(level, std::memory_order_relaxed);
    std::cout << "Cache access level changed to " << static_cast<int>(level) << std::endl;
}

CacheManager::AccessLevel CacheManager::getAccessLevel() const {
    return accessLevel_.load(std::memory_order_relaxed);
}

void CacheManager::setAutoCleanupEnabled(bool enabled) {
    autoCleanupEnabled_.store(enabled, std::memory_order_relaxed);
    std::cout << "Auto cleanup " << (enabled ? "enabled" : "disabled") << std::endl;
}

bool CacheManager::isAutoCleanupEnabled() const {
    return autoCleanupEnabled_.load(std::memory_order_relaxed);
}

bool CacheManager::checkAccessLevel(AccessLevel requiredLevel) const {
    AccessLevel currentLevel = accessLevel_.load(std::memory_order_relaxed);
    return static_cast<int>(currentLevel) >= static_cast<int>(requiredLevel);
}

size_t CacheManager::calculateEntrySize(const CacheEntry& entry) const {
    // Estimate memory usage for a cache entry
    size_t size = sizeof(CacheEntry);
    size += entry.nodeId.capacity();
    size += entry.value.capacity();
    size += entry.status.capacity();
    size += entry.reason.capacity();

    return size;
}

CacheManager::CacheResult CacheManager::getCachedValueWithStatus(const std::string& nodeId) {
    // Check access level
    if (!checkAccessLevel(AccessLevel::READ_ONLY)) {
        std::cout << "Access denied: insufficient permissions for read operation" << std::endl;
        return CacheResult{std::nullopt, CacheStatus::EXPIRED};
    }

    std::shared_lock<std::shared_mutex> lock(cacheMutex_);

    totalReads_.fetch_add(1, std::memory_order_relaxed);

    auto it = cache_.find(nodeId);
    if (it != cache_.end()) {
        // Update last accessed time atomically
        it->second.updateLastAccessed();

        CacheStatus status = evaluateCacheStatus(it->second);
        recordCacheHit(status);

        return CacheResult{it->second, status};
    }

    recordCacheMiss();
    return CacheResult{std::nullopt, CacheStatus::EXPIRED};
}

std::vector<CacheManager::CacheResult> CacheManager::getCachedValuesWithStatus(const std::vector<std::string>& nodeIds) {
    // Check access level
    if (!checkAccessLevel(AccessLevel::READ_ONLY)) {
        std::cout << "Access denied: insufficient permissions for read operation" << std::endl;
        return std::vector<CacheResult>(nodeIds.size(), CacheResult{std::nullopt, CacheStatus::EXPIRED});
    }

    std::shared_lock<std::shared_mutex> lock(cacheMutex_);

    std::vector<CacheResult> results;
    results.reserve(nodeIds.size());

    for (const auto& nodeId : nodeIds) {
        totalReads_.fetch_add(1, std::memory_order_relaxed);

        auto it = cache_.find(nodeId);
        if (it != cache_.end()) {
            // Update last accessed time atomically
            it->second.updateLastAccessed();

            CacheStatus status = evaluateCacheStatus(it->second);
            recordCacheHit(status);

            results.emplace_back(CacheResult{it->second, status});
        } else {
            recordCacheMiss();
            results.emplace_back(CacheResult{std::nullopt, CacheStatus::EXPIRED});
        }
    }

    return results;
}

void CacheManager::updateCacheBatch(const std::vector<ReadResult>& results) {
    // Check access level (lock-free)
    if (!checkAccessLevel(AccessLevel::READ_WRITE)) {
        std::cout << "Access denied: insufficient permissions for write operation" << std::endl;
        return;
    }

    if (results.empty()) {
        return;
    }

    // Increment batch operations counter (lock-free)
    batchOperations_.fetch_add(1, std::memory_order_relaxed);

    // Batch update statistics (lock-free, before acquiring lock)
    totalWrites_.fetch_add(results.size(), std::memory_order_relaxed);

    // Check memory pressure before acquiring write lock
    bool needsEviction = false;
    if (memoryManager_) {
        needsEviction = memoryManager_->hasMemoryPressure() || memoryManager_->hasEntryPressure();
    }

    std::unique_lock<std::shared_mutex> lock(cacheMutex_);

    // Handle memory pressure if needed
    if (needsEviction) {
        size_t evicted = handleMemoryPressure();
        std::cout << "Memory pressure detected during batch update, evicted " << evicted << " entries" << std::endl;
    }

    // Prepare current time once for all new entries
    auto now = std::chrono::steady_clock::now();

    for (const auto& result : results) {
        auto it = cache_.find(result.id);
        if (it != cache_.end()) {
            // Update existing entry (preserve creationTime)
            it->second.value = result.value;
            it->second.status = result.success ? "Good" : "Bad";
            it->second.reason = result.reason;
            it->second.timestamp = result.timestamp;
            it->second.updateLastAccessed(); // Use atomic method
        } else {
            // Create new entry
            CacheEntry entry;
            entry.nodeId = result.id;
            entry.value = result.value;
            entry.status = result.success ? "Good" : "Bad";
            entry.reason = result.reason;
            entry.timestamp = result.timestamp;
            entry.creationTime = now;
            entry.lastAccessed.store(now);
            entry.hasSubscription.store(false);

            cache_[result.id] = entry;
        }
    }

    // Update memory manager (use no-lock version since we already hold the lock)
    if (memoryManager_) {
        memoryManager_->updateCurrentEntryCount(cache_.size());
        memoryManager_->updateCurrentMemoryUsage(getMemoryUsageNoLock());
    }

    // Enforce size limit if necessary
    if (cache_.size() > maxCacheSize_) {
        enforceSizeLimit();
    }

    std::cout << "Batch cache update completed for " << results.size() << " entries" << std::endl;
}

CacheManager::CacheStatus CacheManager::evaluateCacheStatus(const CacheEntry& entry) const {
    auto age = entry.getAge();

    if (age < refreshThreshold_) {
        return CacheStatus::FRESH;
    } else if (age < expireTime_) {
        return CacheStatus::STALE;
    } else {
        return CacheStatus::EXPIRED;
    }
}

void CacheManager::setRefreshThreshold(std::chrono::seconds threshold) {
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    refreshThreshold_ = threshold;
    std::cout << "Cache refresh threshold set to " << threshold.count() << " seconds" << std::endl;
}

void CacheManager::setExpireTime(std::chrono::seconds expireTime) {
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    expireTime_ = expireTime;
    std::cout << "Cache expire time set to " << expireTime.count() << " seconds" << std::endl;
}

void CacheManager::setCleanupInterval(std::chrono::seconds interval) {
    // This is a legacy method for compatibility
    // In the new design, cleanup interval is handled by the background updater
    std::cout << "Cleanup interval set to " << interval.count() << " seconds (legacy method)" << std::endl;
}

void CacheManager::recordCacheHit(CacheStatus status) const {
    totalHits_.fetch_add(1, std::memory_order_relaxed);

    switch (status) {
        case CacheStatus::FRESH:
            freshHits_.fetch_add(1, std::memory_order_relaxed);
            break;
        case CacheStatus::STALE:
            staleHits_.fetch_add(1, std::memory_order_relaxed);
            break;
        case CacheStatus::EXPIRED:
            expiredReads_.fetch_add(1, std::memory_order_relaxed);
            break;
    }
}

void CacheManager::recordCacheMiss() const {
    totalMisses_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t CacheManager::getFreshHits() const {
    return freshHits_.load(std::memory_order_relaxed);
}

uint64_t CacheManager::getStaleHits() const {
    return staleHits_.load(std::memory_order_relaxed);
}

uint64_t CacheManager::getExpiredReads() const {
    return expiredReads_.load(std::memory_order_relaxed);
}

uint64_t CacheManager::getBatchOperations() const {
    return batchOperations_.load(std::memory_order_relaxed);
}

uint64_t CacheManager::getConcurrentReadBlocks() const {
    return concurrentReadBlocks_.load(std::memory_order_relaxed);
}

CacheMemoryManager* CacheManager::getMemoryManager() {
    return memoryManager_.get();
}

const CacheMemoryManager* CacheManager::getMemoryManager() const {
    return memoryManager_.get();
}

size_t CacheManager::evictLRUEntries(size_t targetCount) {
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);

    if (targetCount == 0 || cache_.empty()) {
        return 0;
    }

    // Create vector of entries sorted by last accessed time (oldest first)
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> entries;
    entries.reserve(cache_.size());

    for (const auto& pair : cache_) {
        // Don't evict entries with active subscriptions
        if (!pair.second.getSubscriptionStatus()) {
            entries.emplace_back(pair.first, pair.second.getLastAccessed());
        }
    }

    // Sort by last accessed time (oldest first)
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) {
                  return a.second < b.second;
              });

    // Remove oldest entries
    size_t removedCount = 0;
    size_t toRemove = std::min(targetCount, entries.size());

    for (size_t i = 0; i < toRemove; ++i) {
        auto it = cache_.find(entries[i].first);
        if (it != cache_.end()) {
            std::cout << "LRU evicting cache entry for node " << it->first << std::endl;

            // Trigger eviction callback if set
            if (memoryManager_) {
                memoryManager_->triggerEvictionCallback(it->first, "lru");
            }

            cache_.erase(it);
            ++removedCount;
        }
    }

    // Update memory manager (use no-lock version since we already hold the lock)
    if (memoryManager_ && removedCount > 0) {
        memoryManager_->recordEviction(removedCount, "lru");
        memoryManager_->updateCurrentEntryCount(cache_.size());
        memoryManager_->updateCurrentMemoryUsage(getMemoryUsageNoLock());
    }

    std::cout << "LRU eviction removed " << removedCount << " entries" << std::endl;

    return removedCount;
}

size_t CacheManager::handleMemoryPressure() {
    // This method assumes unique_lock is already held

    if (!memoryManager_ || !memoryManager_->isEnabled()) {
        return 0;
    }

    // Calculate how many entries to evict
    size_t evictionCount = memoryManager_->calculateEvictionCount(0.7); // Target 70% usage

    if (evictionCount == 0) {
        return 0;
    }

    std::cout << "Handling memory pressure, evicting " << evictionCount << " entries" << std::endl;

    // Create vector of entries sorted by last accessed time (oldest first)
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> entries;
    entries.reserve(cache_.size());

    for (const auto& pair : cache_) {
        // Don't evict entries with active subscriptions
        if (!pair.second.getSubscriptionStatus()) {
            entries.emplace_back(pair.first, pair.second.getLastAccessed());
        }
    }

    // Sort by last accessed time (oldest first)
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) {
                  return a.second < b.second;
              });

    // Remove oldest entries
    size_t removedCount = 0;
    size_t toRemove = std::min(evictionCount, entries.size());

    for (size_t i = 0; i < toRemove; ++i) {
        auto it = cache_.find(entries[i].first);
        if (it != cache_.end()) {
            // Trigger eviction callback if set
            memoryManager_->triggerEvictionCallback(it->first, "memory_pressure");

            cache_.erase(it);
            ++removedCount;
        }
    }

    // Update memory manager (use no-lock version since we already hold the lock)
    if (removedCount > 0) {
        memoryManager_->recordEviction(removedCount, "memory_pressure");
        memoryManager_->updateCurrentEntryCount(cache_.size());
        memoryManager_->updateCurrentMemoryUsage(getMemoryUsageNoLock());
    }

    return removedCount;
}

} // namespace opcua2http
