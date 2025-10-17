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
        std::chrono::steady_clock::time_point lastAccessed;   // Last access time for cleanup
        bool hasSubscription;                                 // Whether this node has an active subscription
        
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
     * @brief Access control levels for cache operations
     */
    enum class AccessLevel {
        READ_ONLY,      // Only read operations allowed
        READ_WRITE,     // Both read and write operations allowed
        ADMIN           // All operations including clear and configuration changes
    };

    /**
     * @brief Constructor with configurable cache expiration time
     * @param cacheExpireMinutes Cache expiration time in minutes (default: 60)
     * @param maxCacheSize Maximum number of cache entries (default: 10000)
     */
    explicit CacheManager(int cacheExpireMinutes = 60, size_t maxCacheSize = 10000);

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

private:
    // Cache storage
    mutable std::shared_mutex cacheMutex_;                    // Reader-writer lock for thread safety
    std::unordered_map<std::string, CacheEntry> cache_;      // Main cache storage

    // Configuration
    std::chrono::minutes cacheExpireTime_;                   // Cache expiration time
    size_t maxCacheSize_;                                    // Maximum cache size

    // Statistics (atomic for thread-safe access)
    mutable std::atomic<uint64_t> totalHits_{0};            // Total cache hits
    mutable std::atomic<uint64_t> totalMisses_{0};          // Total cache misses
    mutable std::atomic<uint64_t> totalReads_{0};           // Total read operations
    mutable std::atomic<uint64_t> totalWrites_{0};          // Total write operations
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
     * @brief Update last accessed time for cache entry (assumes write lock held)
     * @param entry Cache entry to update
     */
    void updateLastAccessed(CacheEntry& entry) const;

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
};

} // namespace opcua2http