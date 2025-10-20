#pragma once

#include <string>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_set>
#include <chrono>
#include <memory>
#include "core/IBackgroundUpdater.h"

namespace opcua2http {

// Forward declarations
class CacheManager;
class OPCUAClient;

/**
 * @brief Background updater component for asynchronous cache updates
 * 
 * This component manages background updates for stale cache entries using
 * a worker thread pool and update queue with deduplication logic.
 */
class BackgroundUpdater : public IBackgroundUpdater {
public:
    /**
     * @brief Statistics structure for monitoring background updates
     */
    struct UpdateStats {
        uint64_t totalUpdates{0};           // Total number of updates processed
        uint64_t successfulUpdates{0};      // Number of successful updates
        uint64_t failedUpdates{0};          // Number of failed updates
        uint64_t queuedUpdates{0};          // Current number of queued updates
        uint64_t duplicateUpdates{0};       // Number of duplicate updates filtered
        double averageUpdateTime{0.0};      // Average update time in milliseconds
        std::chrono::steady_clock::time_point lastUpdate; // Last update timestamp
    };

    /**
     * @brief Constructor
     * @param cacheManager Pointer to cache manager for updating cache
     * @param opcClient Pointer to OPC UA client for reading data
     */
    BackgroundUpdater(CacheManager* cacheManager, OPCUAClient* opcClient);

    /**
     * @brief Destructor - ensures proper cleanup of worker threads
     */
    ~BackgroundUpdater();

    // Disable copy constructor and assignment operator
    BackgroundUpdater(const BackgroundUpdater&) = delete;
    BackgroundUpdater& operator=(const BackgroundUpdater&) = delete;

    /**
     * @brief Schedule background update for a single node
     * @param nodeId Node identifier to update
     */
    void scheduleUpdate(const std::string& nodeId) override;

    /**
     * @brief Schedule background updates for multiple nodes
     * @param nodeIds Vector of node identifiers to update
     */
    void scheduleBatchUpdate(const std::vector<std::string>& nodeIds) override;

    /**
     * @brief Start the background updater with worker threads
     */
    void start();

    /**
     * @brief Stop the background updater and wait for threads to finish
     */
    void stop();

    /**
     * @brief Check if the background updater is running
     * @return True if running, false otherwise
     */
    bool isRunning() const;

    /**
     * @brief Set maximum number of concurrent update threads
     * @param maxUpdates Maximum concurrent updates (default: 3)
     */
    void setMaxConcurrentUpdates(size_t maxUpdates);

    /**
     * @brief Set maximum update queue size
     * @param maxQueueSize Maximum queue size (default: 1000)
     */
    void setUpdateQueueSize(size_t maxQueueSize);

    /**
     * @brief Set update timeout for OPC UA operations
     * @param timeout Timeout duration (default: 5000ms)
     */
    void setUpdateTimeout(std::chrono::milliseconds timeout);

    /**
     * @brief Get current update statistics
     * @return UpdateStats structure with current statistics
     */
    UpdateStats getStats() const;

    /**
     * @brief Clear all statistics counters
     */
    void clearStats();

private:
    // Dependencies
    CacheManager* cacheManager_;
    OPCUAClient* opcClient_;

    // Thread management
    std::vector<std::thread> workerThreads_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};

    // Update queue with thread safety
    std::queue<std::string> updateQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;

    // Configuration parameters
    std::atomic<size_t> maxConcurrentUpdates_{3};
    std::atomic<size_t> maxQueueSize_{1000};
    std::atomic<std::chrono::milliseconds> updateTimeout_{std::chrono::milliseconds(5000)};

    // Deduplication mechanism
    std::unordered_set<std::string> pendingUpdates_;
    mutable std::mutex pendingMutex_;

    // Statistics (atomic for thread-safe access)
    mutable std::atomic<uint64_t> totalUpdates_{0};
    mutable std::atomic<uint64_t> successfulUpdates_{0};
    mutable std::atomic<uint64_t> failedUpdates_{0};
    mutable std::atomic<uint64_t> duplicateUpdates_{0};
    mutable std::atomic<double> totalUpdateTime_{0.0};
    std::chrono::steady_clock::time_point lastUpdate_;
    mutable std::mutex statsMutex_;

    /**
     * @brief Worker thread main loop
     */
    void workerLoop();

    /**
     * @brief Process a single update request
     * @param nodeId Node identifier to update
     */
    void processUpdate(const std::string& nodeId);

    /**
     * @brief Add node to pending updates set (with deduplication)
     * @param nodeId Node identifier to add
     * @return True if added (not duplicate), false if already pending
     */
    bool addToPendingUpdates(const std::string& nodeId);

    /**
     * @brief Remove node from pending updates set
     * @param nodeId Node identifier to remove
     */
    void removeFromPendingUpdates(const std::string& nodeId);

    /**
     * @brief Get next update from queue (blocking)
     * @return Node identifier to update, empty string if should stop
     */
    std::string getNextUpdate();

    /**
     * @brief Record update statistics
     * @param success Whether the update was successful
     * @param updateTime Time taken for the update in milliseconds
     */
    void recordUpdateStats(bool success, double updateTime);

    /**
     * @brief Check if queue is full
     * @return True if queue has reached maximum size
     */
    bool isQueueFull() const;

    /**
     * @brief Get current queue size
     * @return Number of items in update queue
     */
    size_t getQueueSize() const;
};

} // namespace opcua2http