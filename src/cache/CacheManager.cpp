#include "cache/CacheManager.h"
#include <algorithm>
#include <iostream>

namespace opcua2http {

CacheManager::CacheManager(int cacheExpireMinutes, size_t maxCacheSize)
    : cacheExpireTime_(cacheExpireMinutes)
    , maxCacheSize_(maxCacheSize)
    , lastCleanup_(std::chrono::steady_clock::now())
    , creationTime_(std::chrono::steady_clock::now()) {
    
    std::cout << "CacheManager initialized with " << cacheExpireMinutes 
              << " minutes expiration and max size " << maxCacheSize << std::endl;
}

std::optional<CacheManager::CacheEntry> CacheManager::getCachedValue(const std::string& nodeId) {
    // Check access level
    if (!checkAccessLevel(AccessLevel::READ_ONLY)) {
        std::cout << "Access denied: insufficient permissions for read operation" << std::endl;
        return std::nullopt;
    }

    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    
    totalReads_.fetch_add(1, std::memory_order_relaxed);
    
    auto it = cache_.find(nodeId);
    if (it != cache_.end()) {
        // Update last accessed time
        const_cast<CacheEntry&>(it->second).lastAccessed = std::chrono::steady_clock::now();
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
        // Update existing entry
        it->second.value = value;
        it->second.status = status;
        it->second.reason = reason;
        it->second.timestamp = timestamp;
        updateLastAccessed(it->second);
        
        std::cout << "Cache updated for node " << nodeId << " with value: " << value << std::endl;
    } else {
        // Create new entry
        CacheEntry entry{
            nodeId,
            value,
            status,
            reason,
            timestamp,
            std::chrono::steady_clock::now(),
            false  // No subscription by default when updating from external source
        };
        
        cache_[nodeId] = entry;
        std::cout << "New cache entry created for node " << nodeId << " with value: " << value << std::endl;
        
        // Enforce size limit if necessary
        if (cache_.size() > maxCacheSize_) {
            enforceSizeLimit();
        }
    }
}

void CacheManager::addCacheEntry(const std::string& nodeId, const CacheEntry& entry) {
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    
    cache_[nodeId] = entry;
    updateLastAccessed(cache_[nodeId]);
    
    std::cout << "Cache entry added for node " << nodeId << std::endl;
    
    // Enforce size limit if necessary
    if (cache_.size() > maxCacheSize_) {
        enforceSizeLimit();
    }
}

void CacheManager::addCacheEntry(const ReadResult& result, bool hasSubscription) {
    CacheEntry entry{
        result.id,
        result.value,
        result.success ? "Good" : "Bad",
        result.reason,
        result.timestamp,
        std::chrono::steady_clock::now(),
        hasSubscription
    };
    
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
        if (!it->second.hasSubscription && it->second.lastAccessed < unusedThreshold) {
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
        if (pair.second.hasSubscription) {
            nodeIds.push_back(pair.first);
        }
    }
    
    return nodeIds;
}

void CacheManager::setSubscriptionStatus(const std::string& nodeId, bool hasSubscription) {
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    
    auto it = cache_.find(nodeId);
    if (it != cache_.end()) {
        it->second.hasSubscription = hasSubscription;
        updateLastAccessed(it->second);
        
        std::cout << "Subscription status for node " << nodeId 
                  << " set to " << (hasSubscription ? "active" : "inactive") << std::endl;
    }
}

CacheManager::CacheStats CacheManager::getStats() const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    
    size_t subscribedCount = 0;
    size_t memoryUsage = 0;
    
    for (const auto& pair : cache_) {
        if (pair.second.hasSubscription) {
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
    return (now - entry.lastAccessed) > cacheExpireTime_;
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
        if (!pair.second.hasSubscription) {
            entries.emplace_back(pair.first, pair.second.lastAccessed);
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

void CacheManager::updateLastAccessed(CacheEntry& entry) const {
    entry.lastAccessed = std::chrono::steady_clock::now();
}

size_t CacheManager::getMemoryUsage() const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    
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

} // namespace opcua2http