#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <unordered_set>
#include <atomic>

#include "cache/CacheManager.h"
#include "opcua/OPCUAClient.h"
#include "core/ReadResult.h"
#include "core/IBackgroundUpdater.h"

namespace opcua2http {

/**
 * @brief ReadStrategy component for intelligent cache-based OPC UA data reading
 * 
 * This class implements the core logic for deciding when to use cached data
 * versus reading from the OPC UA server based on cache age and timing thresholds.
 * It supports batch processing optimization and concurrency control.
 */
class ReadStrategy {
public:
    /**
     * @brief Batch read plan structure for optimized processing
     */
    struct BatchReadPlan {
        std::vector<std::string> freshNodes;      // Return from cache (< refreshThreshold)
        std::vector<std::string> staleNodes;      // Return cache + background update (refreshThreshold < age < expireTime)
        std::vector<std::string> expiredNodes;    // Must read synchronously (> expireTime)
        
        /**
         * @brief Get total number of nodes in the plan
         * @return Total node count
         */
        size_t getTotalNodes() const {
            return freshNodes.size() + staleNodes.size() + expiredNodes.size();
        }
        
        /**
         * @brief Check if plan is empty
         * @return True if no nodes in plan
         */
        bool isEmpty() const {
            return getTotalNodes() == 0;
        }
    };

    /**
     * @brief Constructor
     * @param cacheManager Pointer to cache manager instance
     * @param opcClient Pointer to OPC UA client instance
     */
    ReadStrategy(CacheManager* cacheManager, OPCUAClient* opcClient);

    /**
     * @brief Destructor
     */
    ~ReadStrategy();

    // Disable copy constructor and assignment operator
    ReadStrategy(const ReadStrategy&) = delete;
    ReadStrategy& operator=(const ReadStrategy&) = delete;

    /**
     * @brief Process multiple node requests with intelligent caching
     * @param nodeIds Vector of OPC UA node identifiers to read
     * @return Vector of ReadResults for all requested nodes
     */
    std::vector<ReadResult> processNodeRequests(const std::vector<std::string>& nodeIds);

    /**
     * @brief Process single node request with intelligent caching
     * @param nodeId OPC UA node identifier to read
     * @return ReadResult for the requested node
     */
    ReadResult processNodeRequest(const std::string& nodeId);

    /**
     * @brief Create batch read plan by categorizing nodes based on cache status
     * @param nodeIds Vector of node identifiers to categorize
     * @return BatchReadPlan with nodes categorized by cache status
     */
    BatchReadPlan createBatchPlan(const std::vector<std::string>& nodeIds);

    /**
     * @brief Execute batch read plan with optimized processing
     * @param plan BatchReadPlan to execute
     * @return Vector of ReadResults for all nodes in the plan
     */
    std::vector<ReadResult> executeBatchPlan(const BatchReadPlan& plan);

    /**
     * @brief Schedule background update for a single node (for stale cache entries)
     * @param nodeId Node identifier to update in background
     */
    void scheduleBackgroundUpdate(const std::string& nodeId);

    /**
     * @brief Schedule background updates for multiple nodes
     * @param nodeIds Vector of node identifiers to update in background
     */
    void scheduleBackgroundUpdates(const std::vector<std::string>& nodeIds);

    /**
     * @brief Enable or disable concurrency control for duplicate requests
     * @param enabled Whether concurrency control should be enabled
     */
    void enableConcurrencyControl(bool enabled);

    /**
     * @brief Set maximum number of concurrent reads allowed
     * @param maxReads Maximum concurrent read operations
     */
    void setMaxConcurrentReads(size_t maxReads);

    /**
     * @brief Get current concurrency control status
     * @return True if concurrency control is enabled
     */
    bool isConcurrencyControlEnabled() const;

    /**
     * @brief Get maximum concurrent reads setting
     * @return Maximum concurrent read operations allowed
     */
    size_t getMaxConcurrentReads() const;

    /**
     * @brief Set background updater instance (for dependency injection)
     * @param backgroundUpdater Pointer to background updater instance
     */
    void setBackgroundUpdater(IBackgroundUpdater* backgroundUpdater);

private:
    // Dependencies
    CacheManager* cacheManager_;                              // Cache manager instance
    OPCUAClient* opcClient_;                                  // OPC UA client instance
    IBackgroundUpdater* backgroundUpdater_;                   // Background updater instance (optional)

    // Concurrency control
    mutable std::mutex readMutex_;                           // Mutex for protecting activeReads_
    std::unordered_set<std::string> activeReads_;            // Set of node IDs currently being read
    std::condition_variable readCondition_;                  // Condition variable for waiting on active reads
    std::atomic<bool> concurrencyControlEnabled_{true};     // Whether concurrency control is enabled
    std::atomic<size_t> maxConcurrentReads_{10};            // Maximum concurrent read operations

    /**
     * @brief Acquire read lock for a node ID to prevent duplicate concurrent reads
     * @param nodeId Node identifier to acquire lock for
     * @return True if lock was acquired, false if already locked
     */
    bool acquireReadLock(const std::string& nodeId);

    /**
     * @brief Release read lock for a node ID
     * @param nodeId Node identifier to release lock for
     */
    void releaseReadLock(const std::string& nodeId);

    /**
     * @brief Handle concurrent read scenario (wait for existing read to complete)
     * @param nodeId Node identifier being read concurrently
     * @return ReadResult from the completed concurrent read
     */
    ReadResult handleConcurrentRead(const std::string& nodeId);

    /**
     * @brief Process fresh cache entries (return directly from cache)
     * @param nodeIds Vector of node identifiers with fresh cache entries
     * @return Vector of ReadResults from cache
     */
    std::vector<ReadResult> processFreshNodes(const std::vector<std::string>& nodeIds);

    /**
     * @brief Process stale cache entries (return cache + schedule background update)
     * @param nodeIds Vector of node identifiers with stale cache entries
     * @return Vector of ReadResults from cache
     */
    std::vector<ReadResult> processStaleNodes(const std::vector<std::string>& nodeIds);

    /**
     * @brief Process expired cache entries (synchronous OPC UA read)
     * @param nodeIds Vector of node identifiers with expired/missing cache entries
     * @return Vector of ReadResults from OPC UA server
     */
    std::vector<ReadResult> processExpiredNodes(const std::vector<std::string>& nodeIds);

    /**
     * @brief Read nodes from OPC UA server and update cache
     * @param nodeIds Vector of node identifiers to read
     * @return Vector of ReadResults from OPC UA server
     */
    std::vector<ReadResult> readAndUpdateCache(const std::vector<std::string>& nodeIds);

    /**
     * @brief Create error result for a node
     * @param nodeId Node identifier
     * @param reason Error reason
     * @return ReadResult with error status
     */
    ReadResult createErrorResult(const std::string& nodeId, const std::string& reason);

    /**
     * @brief Get current timestamp in milliseconds
     * @return Current Unix timestamp in milliseconds
     */
    uint64_t getCurrentTimestamp();
};

} // namespace opcua2http