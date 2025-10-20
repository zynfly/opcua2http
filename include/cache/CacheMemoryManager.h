#pragma once

#include <cstddef>
#include <chrono>
#include <atomic>
#include <functional>
#include <vector>
#include <string>

namespace opcua2http {

/**
 * @brief Memory manager for cache with LRU eviction and configurable limits
 *
 * This class manages memory usage for the cache system, implementing LRU
 * (Least Recently Used) eviction when memory pressure occurs. It provides
 * configurable memory limits and monitoring capabilities.
 */
class CacheMemoryManager {
public:
    /**
     * @brief Memory statistics structure
     */
    struct MemoryStats {
        size_t currentMemoryBytes;      // Current memory usage in bytes
        size_t maxMemoryBytes;          // Maximum allowed memory in bytes
        size_t currentEntries;          // Current number of entries
        size_t maxEntries;              // Maximum allowed entries
        double memoryUsageRatio;        // Memory usage ratio (0.0 to 1.0)
        double entryUsageRatio;         // Entry usage ratio (0.0 to 1.0)
        uint64_t totalEvictions;        // Total number of evictions performed
        uint64_t lruEvictions;          // LRU-based evictions
        uint64_t memoryPressureEvictions; // Memory pressure evictions
        std::chrono::steady_clock::time_point lastEviction; // Last eviction time
    };

    /**
     * @brief Eviction callback function type
     * @param nodeId Node ID being evicted
     * @param reason Reason for eviction
     */
    using EvictionCallback = std::function<void(const std::string& nodeId, const std::string& reason)>;

    /**
     * @brief Constructor with configurable limits
     * @param maxMemoryBytes Maximum memory usage in bytes (default: 100MB)
     * @param maxEntries Maximum number of cache entries (default: 10000)
     */
    explicit CacheMemoryManager(size_t maxMemoryBytes = 100 * 1024 * 1024,
                               size_t maxEntries = 10000);

    /**
     * @brief Destructor
     */
    ~CacheMemoryManager() = default;

    // Disable copy constructor and assignment operator
    CacheMemoryManager(const CacheMemoryManager&) = delete;
    CacheMemoryManager& operator=(const CacheMemoryManager&) = delete;

    /**
     * @brief Set maximum memory usage
     * @param maxBytes Maximum memory in bytes
     */
    void setMaxMemoryUsage(size_t maxBytes);

    /**
     * @brief Set maximum number of entries
     * @param maxEntries Maximum entries
     */
    void setMaxEntries(size_t maxEntries);

    /**
     * @brief Get maximum memory usage
     * @return Maximum memory in bytes
     */
    size_t getMaxMemoryUsage() const;

    /**
     * @brief Get maximum number of entries
     * @return Maximum entries
     */
    size_t getMaxEntries() const;

    /**
     * @brief Update current memory usage
     * @param memoryBytes Current memory usage in bytes
     */
    void updateCurrentMemoryUsage(size_t memoryBytes);

    /**
     * @brief Update current entry count
     * @param entryCount Current number of entries
     */
    void updateCurrentEntryCount(size_t entryCount);

    /**
     * @brief Get current memory usage
     * @return Current memory usage in bytes
     */
    size_t getCurrentMemoryUsage() const;

    /**
     * @brief Get current entry count
     * @return Current number of entries
     */
    size_t getCurrentEntryCount() const;

    /**
     * @brief Get memory usage ratio
     * @return Memory usage ratio (0.0 to 1.0)
     */
    double getMemoryUsageRatio() const;

    /**
     * @brief Get entry usage ratio
     * @return Entry usage ratio (0.0 to 1.0)
     */
    double getEntryUsageRatio() const;

    /**
     * @brief Check if memory pressure exists
     * @param threshold Pressure threshold (default: 0.9 = 90%)
     * @return True if memory usage exceeds threshold
     */
    bool hasMemoryPressure(double threshold = 0.9) const;

    /**
     * @brief Check if entry limit pressure exists
     * @param threshold Pressure threshold (default: 0.9 = 90%)
     * @return True if entry count exceeds threshold
     */
    bool hasEntryPressure(double threshold = 0.9) const;

    /**
     * @brief Calculate number of entries to evict to reach target ratio
     * @param targetRatio Target memory usage ratio (default: 0.7 = 70%)
     * @return Number of entries to evict
     */
    size_t calculateEvictionCount(double targetRatio = 0.7) const;

    /**
     * @brief Record eviction event
     * @param count Number of entries evicted
     * @param reason Eviction reason ("lru", "memory_pressure", "size_limit")
     */
    void recordEviction(size_t count, const std::string& reason);

    /**
     * @brief Set eviction callback
     * @param callback Callback function to be called on eviction
     */
    void setEvictionCallback(EvictionCallback callback);

    /**
     * @brief Trigger eviction callback
     * @param nodeId Node ID being evicted
     * @param reason Eviction reason
     */
    void triggerEvictionCallback(const std::string& nodeId, const std::string& reason);

    /**
     * @brief Get memory statistics
     * @return MemoryStats structure with current statistics
     */
    MemoryStats getStats() const;

    /**
     * @brief Reset statistics
     */
    void resetStats();

    /**
     * @brief Enable or disable memory management
     * @param enabled Whether memory management should be enabled
     */
    void setEnabled(bool enabled);

    /**
     * @brief Check if memory management is enabled
     * @return True if memory management is enabled
     */
    bool isEnabled() const;

private:
    // Configuration
    std::atomic<size_t> maxMemoryBytes_;        // Maximum memory usage
    std::atomic<size_t> maxEntries_;            // Maximum number of entries
    std::atomic<bool> enabled_{true};           // Whether memory management is enabled

    // Current state
    std::atomic<size_t> currentMemoryBytes_{0}; // Current memory usage
    std::atomic<size_t> currentEntries_{0};     // Current number of entries

    // Statistics
    std::atomic<uint64_t> totalEvictions_{0};   // Total evictions
    std::atomic<uint64_t> lruEvictions_{0};     // LRU evictions
    std::atomic<uint64_t> memoryPressureEvictions_{0}; // Memory pressure evictions
    std::chrono::steady_clock::time_point lastEviction_; // Last eviction time

    // Callback
    EvictionCallback evictionCallback_;
};

} // namespace opcua2http
