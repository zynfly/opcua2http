#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <vector>
#include <chrono>
#include <shared_mutex>
#include <atomic>
#include <cstdint>
#include "core/ReadResult.h"

namespace opcua2http {

/**
 * @brief Thread-safe cache manager for OPC UA data points
 * 
 * This class manages a map-based cache of OPC UA node values with automatic
 * expiration and cleanup mechanisms. It provides thread-safe access using
 * shared_mutex for optimal read performance.
 */
class CacheManager {
public:
    /**
     * @brief Cache entry structure containing all cached data for a node
     */
    struct CacheEntry {
        std::string nodeId;                                    // OPC UA node identifier
        std::string value;                                     // Cached value as string
        std::string status;                                    // Status code (e.g., "Good", "Bad")
        std::string reason;                                    // Status description
        uint64_t timestamp;                                    // Unix timestamp in milliseconds
        std::chrono::steady_clock::time_point creationTime;   // Cache entry creation time
        mutable std::atomic<std::chrono::steady_clock::time_point> lastAccessed; // Last access time (atomic for lock-free updates)
        std::atomic<bool> hasSubscription;                    // Whether this node has an active subscription (atomic)
        
        // Custom constructors and assignment operators for atomic members
        CacheEntry() = default;
        
        CacheEntry(const CacheEntry& other) 
            : nodeId(other.nodeId)
            , value(other.value)
            , status(other.status)
            , reason(other.reason)
            , timestamp(other.timestamp)
            , creationTime(other.creationTime)
            , lastAccessed(other.lastAccessed.load())
            , hasSubscription(other.hasSubscription.load()) {}
            
        CacheEntry(CacheEntry&& other) noexcept
            : nodeId(std::move(other.nodeId))
            , value(std::move(other.value))
            , status(std::move(other.status))
            , reason(std::move(other.reason))
            , timestamp(other.timestamp)
            , creationTime(other.creationTime)
            , lastAccessed(other.lastAccessed.load())
            , hasSubscription(other.hasSubscription.load()) {}
            
        CacheEntry& operator=(const CacheEntry& other) {
            if (this != &other) {
                nodeId = other.nodeId;
                value = other.value;
                status = other.status;
                reason = other.reason;
                timestamp = other.timestamp;
                creationTime = other.creationTime;
                lastAccessed.store(other.lastAccessed.load());
                hasSubscription.store(other.hasSubscription.load());
            }
            return *this;
        }
        
        CacheEntry& operator=(CacheEntry&& other) noexcept {
            if (this != &other) {
                nodeId = std::move(other.nodeId);
                value = std::move(other.value);
                status = std::move(other.status);
                reason = std::move(other.reason);
                timestamp = other.timestamp;
                creationTime = other.creationTime;
                lastAccessed.store(other.lastAccessed.load());
                hasSubscription.store(other.hasSubscription.load());
            }
            return *this;
        }
        
        /**
         * @brief Convert cache entry to ReadResult
         * @return ReadResult structure for API response
         */
        ReadResult toReadResult() const {
            return ReadResult{
                nodeId,
                status == "Good",
                reason,
                value,
                timestamp
            };
        }
        
        /**
         * @brief Update last accessed time atomically (lock-free)
         */
        void updateLastAccessed() const {
            lastAccessed.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
        }
        
        /**
         * @brief Get last accessed time atomically
         * @return Last accessed time point
         */
        std::chrono::steady_clock::time_point getLastAccessed() const {
            return lastAccessed.load(std::memory_order_relaxed);
        }
        
        /**
         * @brief Set subscription status atomically
         * @param subscriptionStatus New subscription status
         */
        void setSubscriptionStatus(bool subscriptionStatus) {
            hasSubscription.store(subscriptionStatus, std::memory_order_relaxed);
        }
        
        /**
         * @brief Get subscription status atomically
         * @return Current subscription status
         */
        bool getSubscriptionStatus() const {
            return hasSubscription.load(std::memory_order_relaxed);
        }
        
        /**
         * @brief Check if cache entry is within refresh threshold
         * @param threshold Refresh threshold duration
         * @return True if entry age is within refresh threshold
         */
        bool isWithinRefreshThreshold(std::chrono::seconds threshold) const {
            auto age = getAge();
            return age < threshold;
        }
        
        /**
         * @brief Check if cache entry is expired
         * @param expireTime Expiration duration
         * @return True if entry is expired
         */
        bool isExpired(std::chrono::seconds expireTime) const {
            auto age = getAge();
            return age >= expireTime;
        }
        
        /**
         * @brief Get age of cache entry
         * @return Duration since creation
         */
        std::chrono::seconds getAge() const {
            auto now = std::chrono::steady_clock::now();
            auto duration = now - creationTime;
            return std::chrono::duration_cast<std::chrono::seconds>(duration);
        }
    };

    /**
     * @brief Cache statistics for monitoring
     */
    struct CacheStats {
        size_t totalEntries;           // Total number of cached entries
        size_t subscribedEntries;      // Number of entries with active subscriptions
        size_t expiredEntries;         // Number of expired entries (last cleanup)
        uint64_t totalHits;            // Total cache hits
        uint64_t totalMisses;          // Total cache misses
        uint64_t totalReads;           // Total read operations
        uint64_t totalWrites;          // Total write operations
        size_t memoryUsageBytes;       // Estimated memory usage in bytes
        double hitRatio;               // Cache hit ratio (hits / (hits + misses))
        std::chrono::steady_clock::time_point lastCleanup; // Last cleanup time
        std::chrono::steady_clock::time_point creationTime; // Cache creation time
    };

    /**
     * @brief Cache status enumeration for smart cache timing
     */
    enum class CacheStatus {
        FRESH,      // < refreshThreshold, use directly
        STALE,      // refreshThreshold < age < expireTime, return cache + background update
        EXPIRED     // > expireTime, must refresh synchronously
    };

    /**
     * @brief Cache result structure with status evaluation
     */
    struct CacheResult {
        std::optional<CacheEntry> entry;
        CacheStatus status;
    };

    /**
     * @brief Access control levels for cache operations
     */
    enum class AccessLevel {
        READ_ONLY,      // Only read operations allowed
        READ_WRITE,     // Both read and write operations allowed
        ADMIN           // All operations including clear and configuration changes
    };

    /**
     * @brief Constructor with configurable cache timing parameters
     * @param cacheExpireMinutes Cache expiration time in minutes (default: 60, legacy)
     * @param maxCacheSize Maximum number of cache entries (default: 10000)
     * @param refreshThresholdSeconds Refresh threshold in seconds (default: 3)
     * @param expireTimeSeconds Expiration time in seconds (default: 10)
     */
    explicit CacheManager(int cacheExpireMinutes = 60, 
                         size_t maxCacheSize = 10000,
                         int refreshThresholdSeconds = 3,
                         int expireTimeSeconds = 10);

    /**
     * @brief Destructor
     */
    ~CacheManager() = default;

    // Disable copy constructor and assignment operator
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    /**
     * @brief Get cached value for a node ID
     * @param nodeId OPC UA node identifier
     * @return Optional cache entry if found, nullopt otherwise
     */
    std::optional<CacheEntry> getCachedValue(const std::string& nodeId);

    /**
     * @brief Get cached value with status evaluation for intelligent cache decisions
     * @param nodeId OPC UA node identifier
     * @return CacheResult with entry (if found) and cache status
     */
    CacheResult getCachedValueWithStatus(const std::string& nodeId);

    /**
     * @brief Get cached values with status evaluation for batch operations
     * @param nodeIds Vector of OPC UA node identifiers
     * @return Vector of CacheResult with entries and cache status
     */
    std::vector<CacheResult> getCachedValuesWithStatus(const std::vector<std::string>& nodeIds);

    /**
     * @brief Update cache with new data (typically from subscription callback)
     * @param nodeId OPC UA node identifier
     * @param value New value as string
     * @param status Status code (e.g., "Good", "Bad")
     * @param reason Status description
     * @param timestamp Unix timestamp in milliseconds
     */
    void updateCache(const std::string& nodeId, 
                    const std::string& value, 
                    const std::string& status, 
                    const std::string& reason, 
                    uint64_t timestamp);

    /**
     * @brief Update cache with batch of ReadResults
     * @param results Vector of ReadResults to update in cache
     */
    void updateCacheBatch(const std::vector<ReadResult>& results);

    /**
     * @brief Add new cache entry
     * @param nodeId OPC UA node identifier
     * @param entry Cache entry to add
     */
    void addCacheEntry(const std::string& nodeId, const CacheEntry& entry);

    /**
     * @brief Add cache entry from ReadResult
     * @param result ReadResult to cache
     * @param hasSubscription Whether this node has an active subscription
     */
    void addCacheEntry(const ReadResult& result, bool hasSubscription = false);

    /**
     * @brief Remove cache entry
     * @param nodeId OPC UA node identifier to remove
     * @return True if entry was removed, false if not found
     */
    bool removeCacheEntry(const std::string& nodeId);

    /**
     * @brief Clean up expired cache entries
     * @return Number of entries removed
     */
    size_t cleanupExpiredEntries();

    /**
     * @brief Clean up entries without subscriptions that haven't been accessed recently
     * @return Number of entries removed
     */
    size_t cleanupUnusedEntries();

    /**
     * @brief Get all cached node IDs
     * @return Vector of all cached node identifiers
     */
    std::vector<std::string> getCachedNodeIds() const;

    /**
     * @brief Get node IDs with active subscriptions
     * @return Vector of node identifiers that have subscriptions
     */
    std::vector<std::string> getSubscribedNodeIds() const;

    /**
     * @brief Mark a cache entry as having a subscription
     * @param nodeId OPC UA node identifier
     * @param hasSubscription Whether the node has an active subscription
     */
    void setSubscriptionStatus(const std::string& nodeId, bool hasSubscription);

    /**
     * @brief Get cache statistics
     * @return CacheStats structure with current statistics
     */
    CacheStats getStats() const;

    /**
     * @brief Clear all cache entries
     */
    void clear();

    /**
     * @brief Get current cache size
     * @return Number of entries in cache
     */
    size_t size() const;

    /**
     * @brief Check if cache is empty
     * @return True if cache is empty
     */
    bool empty() const;

    /**
     * @brief Check if cache has reached maximum size
     * @return True if cache is at maximum capacity
     */
    bool isFull() const;

    /**
     * @brief Get estimated memory usage of cache
     * @return Estimated memory usage in bytes
     */
    size_t getMemoryUsage() const;

    /**
     * @brief Get cache hit ratio
     * @return Hit ratio as percentage (0.0 to 1.0)
     */
    double getHitRatio() const;

    /**
     * @brief Get fresh cache hits count
     * @return Number of fresh cache hits
     */
    uint64_t getFreshHits() const;

    /**
     * @brief Get stale cache hits count
     * @return Number of stale cache hits
     */
    uint64_t getStaleHits() const;

    /**
     * @brief Get expired reads count
     * @return Number of expired cache reads
     */
    uint64_t getExpiredReads() const;

    /**
     * @brief Set cache access level for operations
     * @param level Access level to set
     */
    void setAccessLevel(AccessLevel level);

    /**
     * @brief Get current access level
     * @return Current access level
     */
    AccessLevel getAccessLevel() const;

    /**
     * @brief Enable or disable automatic cleanup
     * @param enabled Whether automatic cleanup should be enabled
     */
    void setAutoCleanupEnabled(bool enabled);

    /**
     * @brief Check if automatic cleanup is enabled
     * @return True if automatic cleanup is enabled
     */
    bool isAutoCleanupEnabled() const;

    /**
     * @brief Set refresh threshold for smart caching
     * @param threshold Refresh threshold duration
     */
    void setRefreshThreshold(std::chrono::seconds threshold);

    /**
     * @brief Set expiration time for smart caching
     * @param expireTime Expiration duration
     */
    void setExpireTime(std::chrono::seconds expireTime);

    /**
     * @brief Set cleanup interval (legacy method for compatibility)
     * @param interval Cleanup interval duration
     */
    void setCleanupInterval(std::chrono::seconds interval);

private:
    // Cache storage
    mutable std::shared_mutex cacheMutex_;                    // Reader-writer lock for thread safety
    std::unordered_map<std::string, CacheEntry> cache_;      // Main cache storage

    // Configuration
    std::chrono::minutes cacheExpireTime_;                   // Cache expiration time (legacy)
    std::chrono::seconds refreshThreshold_;                  // Refresh threshold for smart caching
    std::chrono::seconds expireTime_;                        // Expiration time for smart caching
    size_t maxCacheSize_;                                    // Maximum cache size

    // Statistics (atomic for thread-safe access)
    mutable std::atomic<uint64_t> totalHits_{0};            // Total cache hits
    mutable std::atomic<uint64_t> totalMisses_{0};          // Total cache misses
    mutable std::atomic<uint64_t> totalReads_{0};           // Total read operations
    mutable std::atomic<uint64_t> totalWrites_{0};          // Total write operations
    mutable std::atomic<uint64_t> freshHits_{0};            // Fresh cache hits
    mutable std::atomic<uint64_t> staleHits_{0};            // Stale cache hits
    mutable std::atomic<uint64_t> expiredReads_{0};         // Expired cache reads
    mutable std::atomic<uint64_t> batchOperations_{0};      // Batch operations count
    mutable std::atomic<uint64_t> concurrentReadBlocks_{0}; // Concurrent read blocks count
    std::chrono::steady_clock::time_point lastCleanup_;     // Last cleanup time
    std::chrono::steady_clock::time_point creationTime_;    // Cache creation time

    // Access control
    std::atomic<AccessLevel> accessLevel_{AccessLevel::READ_WRITE}; // Current access level
    std::atomic<bool> autoCleanupEnabled_{true};           // Whether automatic cleanup is enabled

    /**
     * @brief Check if cache entry is expired
     * @param entry Cache entry to check
     * @return True if entry is expired
     */
    bool isExpired(const CacheEntry& entry) const;

    /**
     * @brief Enforce cache size limit by removing oldest entries
     * @return Number of entries removed
     */
    size_t enforceSizeLimit();

    /**
     * @brief Get batch operations count
     * @return Number of batch operations performed
     */
    uint64_t getBatchOperations() const;
    
    /**
     * @brief Get concurrent read blocks count
     * @return Number of times concurrent reads were blocked
     */
    uint64_t getConcurrentReadBlocks() const;

    /**
     * @brief Check access level for operation
     * @param requiredLevel Minimum required access level
     * @return True if current access level allows the operation
     */
    bool checkAccessLevel(AccessLevel requiredLevel) const;

    /**
     * @brief Calculate estimated memory usage for a cache entry
     * @param entry Cache entry to calculate size for
     * @return Estimated memory usage in bytes
     */
    size_t calculateEntrySize(const CacheEntry& entry) const;

    /**
     * @brief Evaluate cache status based on entry age and timing configuration
     * @param entry Cache entry to evaluate
     * @return CacheStatus (FRESH, STALE, or EXPIRED)
     */
    CacheStatus evaluateCacheStatus(const CacheEntry& entry) const;

    /**
     * @brief Record cache hit statistics (lock-free)
     * @param status Cache status for the hit
     */
    void recordCacheHit(CacheStatus status) const;

    /**
     * @brief Record cache miss statistics (lock-free)
     */
    void recordCacheMiss() const;
};

} // namespace opcua2http