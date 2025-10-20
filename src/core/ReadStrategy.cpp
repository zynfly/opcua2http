#include "core/ReadStrategy.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>

namespace opcua2http {

ReadStrategy::ReadStrategy(CacheManager* cacheManager, OPCUAClient* opcClient,
                          CacheErrorHandler* errorHandler)
    : cacheManager_(cacheManager)
    , opcClient_(opcClient)
    , backgroundUpdater_(nullptr)
    , errorHandler_(errorHandler) {

    if (!cacheManager_) {
        throw std::invalid_argument("CacheManager cannot be null");
    }
    if (!opcClient_) {
        throw std::invalid_argument("OPCUAClient cannot be null");
    }

    spdlog::debug("ReadStrategy initialized with cache manager and OPC client");
}

ReadStrategy::~ReadStrategy() {
    // Wait for any active reads to complete
    std::unique_lock<std::mutex> lock(readMutex_);
    readCondition_.wait(lock, [this] { return activeReads_.empty(); });
    spdlog::debug("ReadStrategy destroyed");
}

std::vector<ReadResult> ReadStrategy::processNodeRequests(const std::vector<std::string>& nodeIds) {
    if (nodeIds.empty()) {
        spdlog::warn("Empty node IDs list provided to processNodeRequests");
        return {};
    }

    spdlog::debug("Processing {} node requests", nodeIds.size());

    // Create batch plan based on cache status
    BatchReadPlan plan = createBatchPlan(nodeIds);

    spdlog::debug("Batch plan created: {} fresh, {} stale, {} expired nodes",
                  plan.freshNodes.size(), plan.staleNodes.size(), plan.expiredNodes.size());

    // Execute the batch plan
    return executeBatchPlan(plan);
}

ReadResult ReadStrategy::processNodeRequest(const std::string& nodeId) {
    if (nodeId.empty()) {
        spdlog::warn("Empty node ID provided to processNodeRequest");
        return createErrorResult(nodeId, "Invalid node ID");
    }

    spdlog::debug("Processing single node request: {}", nodeId);

    // Check for concurrent read if concurrency control is enabled
    if (concurrencyControlEnabled_.load()) {
        if (!acquireReadLock(nodeId)) {
            spdlog::debug("Concurrent read detected for node {}, waiting for completion", nodeId);
            return handleConcurrentRead(nodeId);
        }
    }

    try {
        // Get cache status for the node
        auto cacheResult = cacheManager_->getCachedValueWithStatus(nodeId);

        ReadResult result;

        switch (cacheResult.status) {
            case CacheManager::CacheStatus::FRESH:
                spdlog::debug("Node {} has fresh cache, returning cached value", nodeId);
                if (cacheResult.entry.has_value()) {
                    result = cacheResult.entry->toReadResult();
                } else {
                    result = createErrorResult(nodeId, "Fresh cache entry not found");
                }
                break;

            case CacheManager::CacheStatus::STALE:
                spdlog::debug("Node {} has stale cache, returning cached value and scheduling background update", nodeId);
                if (cacheResult.entry.has_value()) {
                    result = cacheResult.entry->toReadResult();
                    // Schedule background update for stale data
                    scheduleBackgroundUpdate(nodeId);
                } else {
                    result = createErrorResult(nodeId, "Stale cache entry not found");
                }
                break;

            case CacheManager::CacheStatus::EXPIRED:
                spdlog::debug("Node {} has expired/missing cache, reading from OPC UA server", nodeId);
                // Read synchronously from OPC UA server
                try {
                    result = opcClient_->readNode(nodeId);
                    if (result.success) {
                        // Update cache with new data
                        cacheManager_->updateCache(nodeId, result.value,
                                                 result.success ? "Good" : "Bad",
                                                 result.reason, result.timestamp);
                    } else if (errorHandler_) {
                        // If read failed, try cache fallback through error handler
                        auto cachedData = cacheManager_->getCachedValue(nodeId);
                        if (cachedData.has_value()) {
                            result = errorHandler_->handleConnectionError(nodeId, cachedData);
                        }
                    }
                } catch (const std::exception& readEx) {
                    spdlog::error("Exception reading node {}: {}", nodeId, readEx.what());
                    if (errorHandler_) {
                        auto cachedData = cacheManager_->getCachedValue(nodeId);
                        result = errorHandler_->handleConnectionError(nodeId, cachedData);
                    } else {
                        result = createErrorResult(nodeId, std::string("Read error: ") + readEx.what());
                    }
                }
                break;
        }

        if (concurrencyControlEnabled_.load()) {
            releaseReadLock(nodeId);
        }

        return result;

    } catch (const std::exception& e) {
        spdlog::error("Error processing node request for {}: {}", nodeId, e.what());
        if (concurrencyControlEnabled_.load()) {
            releaseReadLock(nodeId);
        }
        return createErrorResult(nodeId, std::string("Processing error: ") + e.what());
    }
}

ReadStrategy::BatchReadPlan ReadStrategy::createBatchPlan(const std::vector<std::string>& nodeIds) {
    BatchReadPlan plan;

    if (nodeIds.empty()) {
        return plan;
    }

    // Get cache status for all nodes
    auto cacheResults = cacheManager_->getCachedValuesWithStatus(nodeIds);

    // Categorize nodes based on cache status
    for (size_t i = 0; i < nodeIds.size() && i < cacheResults.size(); ++i) {
        const auto& nodeId = nodeIds[i];
        const auto& cacheResult = cacheResults[i];

        switch (cacheResult.status) {
            case CacheManager::CacheStatus::FRESH:
                plan.freshNodes.push_back(nodeId);
                break;
            case CacheManager::CacheStatus::STALE:
                plan.staleNodes.push_back(nodeId);
                break;
            case CacheManager::CacheStatus::EXPIRED:
                plan.expiredNodes.push_back(nodeId);
                break;
        }
    }

    spdlog::debug("Batch plan created for {} nodes: {} fresh, {} stale, {} expired",
                  nodeIds.size(), plan.freshNodes.size(), plan.staleNodes.size(), plan.expiredNodes.size());

    return plan;
}

std::vector<ReadResult> ReadStrategy::executeBatchPlan(const BatchReadPlan& plan) {
    std::vector<ReadResult> results;

    if (plan.isEmpty()) {
        spdlog::debug("Empty batch plan, returning empty results");
        return results;
    }

    // Reserve space for all results
    results.reserve(plan.getTotalNodes());

    // Process fresh nodes (return from cache)
    if (!plan.freshNodes.empty()) {
        auto freshResults = processFreshNodes(plan.freshNodes);
        results.insert(results.end(), freshResults.begin(), freshResults.end());
    }

    // Process stale nodes (return cache + background update)
    if (!plan.staleNodes.empty()) {
        auto staleResults = processStaleNodes(plan.staleNodes);
        results.insert(results.end(), staleResults.begin(), staleResults.end());
    }

    // Process expired nodes (synchronous OPC UA read)
    if (!plan.expiredNodes.empty()) {
        auto expiredResults = processExpiredNodes(plan.expiredNodes);
        results.insert(results.end(), expiredResults.begin(), expiredResults.end());
    }

    spdlog::debug("Batch plan executed, returning {} results", results.size());
    return results;
}

void ReadStrategy::scheduleBackgroundUpdate(const std::string& nodeId) {
    if (backgroundUpdater_) {
        backgroundUpdater_->scheduleUpdate(nodeId);
        spdlog::debug("Scheduled background update for node: {}", nodeId);
    } else {
        spdlog::warn("Background updater not available, skipping background update for node: {}", nodeId);
    }
}

void ReadStrategy::scheduleBackgroundUpdates(const std::vector<std::string>& nodeIds) {
    if (backgroundUpdater_) {
        backgroundUpdater_->scheduleBatchUpdate(nodeIds);
        spdlog::debug("Scheduled background updates for {} nodes", nodeIds.size());
    } else {
        spdlog::warn("Background updater not available, skipping background updates for {} nodes", nodeIds.size());
    }
}

void ReadStrategy::enableConcurrencyControl(bool enabled) {
    concurrencyControlEnabled_.store(enabled);
    spdlog::info("Concurrency control {}", enabled ? "enabled" : "disabled");
}

void ReadStrategy::setMaxConcurrentReads(size_t maxReads) {
    maxConcurrentReads_.store(maxReads);
    spdlog::info("Maximum concurrent reads set to {}", maxReads);
}

bool ReadStrategy::isConcurrencyControlEnabled() const {
    return concurrencyControlEnabled_.load();
}

size_t ReadStrategy::getMaxConcurrentReads() const {
    return maxConcurrentReads_.load();
}

void ReadStrategy::setBackgroundUpdater(IBackgroundUpdater* backgroundUpdater) {
    backgroundUpdater_ = backgroundUpdater;
    spdlog::debug("Background updater {} set", backgroundUpdater ? "instance" : "null");
}

void ReadStrategy::setErrorHandler(CacheErrorHandler* errorHandler) {
    errorHandler_ = errorHandler;
    spdlog::debug("Error handler {} set", errorHandler ? "instance" : "null");
}

bool ReadStrategy::acquireReadLock(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(readMutex_);

    // Check if this node is already being read
    if (activeReads_.find(nodeId) != activeReads_.end()) {
        return false; // Lock not acquired, concurrent read in progress
    }

    // Check if we've reached the maximum concurrent reads limit
    if (activeReads_.size() >= maxConcurrentReads_.load()) {
        return false; // Lock not acquired, too many concurrent reads
    }

    // Acquire the lock
    activeReads_.insert(nodeId);
    spdlog::debug("Acquired read lock for node: {} (active reads: {})", nodeId, activeReads_.size());
    return true;
}

void ReadStrategy::releaseReadLock(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(readMutex_);

    auto it = activeReads_.find(nodeId);
    if (it != activeReads_.end()) {
        activeReads_.erase(it);
        spdlog::debug("Released read lock for node: {} (active reads: {})", nodeId, activeReads_.size());
        readCondition_.notify_all(); // Notify waiting threads
    }
}

ReadResult ReadStrategy::handleConcurrentRead(const std::string& nodeId) {
    // Wait for the concurrent read to complete
    std::unique_lock<std::mutex> lock(readMutex_);
    readCondition_.wait(lock, [this, &nodeId] {
        return activeReads_.find(nodeId) == activeReads_.end();
    });

    spdlog::debug("Concurrent read completed for node: {}, checking cache", nodeId);

    // The concurrent read should have updated the cache, try to get the result
    auto cacheResult = cacheManager_->getCachedValueWithStatus(nodeId);
    if (cacheResult.entry.has_value()) {
        return cacheResult.entry->toReadResult();
    } else {
        // If still no cache entry, perform our own read
        spdlog::warn("No cache entry found after concurrent read for node: {}, performing own read", nodeId);
        return opcClient_->readNode(nodeId);
    }
}

std::vector<ReadResult> ReadStrategy::processFreshNodes(const std::vector<std::string>& nodeIds) {
    std::vector<ReadResult> results;
    results.reserve(nodeIds.size());

    for (const auto& nodeId : nodeIds) {
        auto cacheEntry = cacheManager_->getCachedValue(nodeId);
        if (cacheEntry.has_value()) {
            results.push_back(cacheEntry->toReadResult());
            spdlog::debug("Returned fresh cached value for node: {}", nodeId);
        } else {
            results.push_back(createErrorResult(nodeId, "Fresh cache entry not found"));
            spdlog::warn("Fresh cache entry not found for node: {}", nodeId);
        }
    }

    return results;
}

std::vector<ReadResult> ReadStrategy::processStaleNodes(const std::vector<std::string>& nodeIds) {
    std::vector<ReadResult> results;
    results.reserve(nodeIds.size());

    // Return cached values
    for (const auto& nodeId : nodeIds) {
        auto cacheEntry = cacheManager_->getCachedValue(nodeId);
        if (cacheEntry.has_value()) {
            results.push_back(cacheEntry->toReadResult());
            spdlog::debug("Returned stale cached value for node: {}", nodeId);
        } else {
            results.push_back(createErrorResult(nodeId, "Stale cache entry not found"));
            spdlog::warn("Stale cache entry not found for node: {}", nodeId);
        }
    }

    // Schedule background updates for all stale nodes
    scheduleBackgroundUpdates(nodeIds);

    return results;
}

std::vector<ReadResult> ReadStrategy::processExpiredNodes(const std::vector<std::string>& nodeIds) {
    if (nodeIds.empty()) {
        return {};
    }

    spdlog::debug("Reading {} expired nodes from OPC UA server", nodeIds.size());

    // Read from OPC UA server and update cache
    return readAndUpdateCache(nodeIds);
}

std::vector<ReadResult> ReadStrategy::readAndUpdateCache(const std::vector<std::string>& nodeIds) {
    std::vector<ReadResult> results;

    try {
        // Use batch read if available, otherwise read individually
        if (nodeIds.size() > 1) {
            results = opcClient_->readNodes(nodeIds);
        } else if (nodeIds.size() == 1) {
            results.push_back(opcClient_->readNode(nodeIds[0]));
        }

        // Update cache with results
        if (!results.empty()) {
            cacheManager_->updateCacheBatch(results);
            spdlog::debug("Updated cache with {} read results", results.size());
        }

        // If error handler is available, handle partial batch failures
        if (errorHandler_ && !results.empty()) {
            results = errorHandler_->handlePartialBatchFailure(nodeIds, results);
        }

    } catch (const std::exception& e) {
        spdlog::error("Error reading nodes from OPC UA server: {}", e.what());

        // If error handler is available, try cache fallback
        if (errorHandler_) {
            results.clear();
            results.reserve(nodeIds.size());

            for (const auto& nodeId : nodeIds) {
                auto cachedData = cacheManager_->getCachedValue(nodeId);
                ReadResult result = errorHandler_->handleConnectionError(nodeId, cachedData);
                results.push_back(result);
            }
        } else {
            // No error handler, create error results
            results.clear();
            results.reserve(nodeIds.size());
            for (const auto& nodeId : nodeIds) {
                results.push_back(createErrorResult(nodeId, std::string("OPC UA read error: ") + e.what()));
            }
        }
    }

    return results;
}

ReadResult ReadStrategy::createErrorResult(const std::string& nodeId, const std::string& reason) {
    return ReadResult::createError(nodeId, reason, getCurrentTimestamp());
}

uint64_t ReadStrategy::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

} // namespace opcua2http
