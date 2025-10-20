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
                // Path 1: FRESH cache (< 3 seconds) - Return cached data immediately
                spdlog::info("[CACHE_PATH:FRESH] Node {} has fresh cache (< 3s), returning cached value immediately", nodeId);
                if (cacheResult.entry.has_value()) {
                    result = cacheResult.entry->toReadResult();
                } else {
                    spdlog::error("[CACHE_PATH:FRESH] Fresh cache entry not found for node {}", nodeId);
                    result = createErrorResult(nodeId, "Fresh cache entry not found");
                }
                break;

            case CacheManager::CacheStatus::STALE:
                // Path 2: STALE cache (3-10 seconds) - Return cached data immediately + schedule background update
                spdlog::info("[CACHE_PATH:STALE] Node {} has stale cache (3-10s), returning cached value and scheduling background update", nodeId);
                if (cacheResult.entry.has_value()) {
                    result = cacheResult.entry->toReadResult();
                    // Schedule background update for stale data (non-blocking)
                    scheduleBackgroundUpdate(nodeId);
                    spdlog::debug("[CACHE_PATH:STALE] Background update scheduled for node {}", nodeId);
                } else {
                    spdlog::error("[CACHE_PATH:STALE] Stale cache entry not found for node {}", nodeId);
                    result = createErrorResult(nodeId, "Stale cache entry not found");
                }
                break;

            case CacheManager::CacheStatus::EXPIRED:
                // Path 3: EXPIRED/MISSING cache (> 10 seconds or no cache) - Synchronously read from OPC UA server
                if (cacheResult.entry.has_value()) {
                    spdlog::info("[CACHE_PATH:EXPIRED] Node {} has expired cache (> 10s), reading synchronously from OPC UA server", nodeId);
                } else {
                    spdlog::info("[CACHE_PATH:MISS] Node {} has no cache data, reading synchronously from OPC UA server", nodeId);
                }

                // Read synchronously from OPC UA server
                try {
                    result = opcClient_->readNode(nodeId);
                    if (result.success) {
                        // Update cache with fresh data
                        cacheManager_->updateCache(nodeId, result.value,
                                                 "Good",
                                                 result.reason, result.timestamp);
                        spdlog::debug("[CACHE_PATH:EXPIRED/MISS] Successfully read and updated cache for node {}", nodeId);
                    } else {
                        spdlog::warn("[CACHE_PATH:EXPIRED/MISS] OPC UA read failed for node {}: {}", nodeId, result.reason);
                        // If read failed, try cache fallback through error handler
                        if (errorHandler_) {
                            auto cachedData = cacheManager_->getCachedValue(nodeId);
                            if (cachedData.has_value()) {
                                result = errorHandler_->handleConnectionError(nodeId, cachedData);
                                spdlog::info("[CACHE_PATH:EXPIRED/MISS] Using cached fallback data for node {}", nodeId);
                            }
                        }
                    }
                } catch (const std::exception& readEx) {
                    spdlog::error("[CACHE_PATH:EXPIRED/MISS] Exception reading node {}: {}", nodeId, readEx.what());
                    if (errorHandler_) {
                        auto cachedData = cacheManager_->getCachedValue(nodeId);
                        result = errorHandler_->handleConnectionError(nodeId, cachedData);
                        if (cachedData.has_value()) {
                            spdlog::info("[CACHE_PATH:EXPIRED/MISS] Using cached fallback data after exception for node {}", nodeId);
                        }
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

    spdlog::info("[CACHE_PATH:FRESH_BATCH] Processing {} fresh nodes (< 3s), returning cached values immediately", nodeIds.size());

    for (const auto& nodeId : nodeIds) {
        auto cacheEntry = cacheManager_->getCachedValue(nodeId);
        if (cacheEntry.has_value()) {
            results.push_back(cacheEntry->toReadResult());
            spdlog::debug("[CACHE_PATH:FRESH] Returned fresh cached value for node: {}", nodeId);
        } else {
            results.push_back(createErrorResult(nodeId, "Fresh cache entry not found"));
            spdlog::warn("[CACHE_PATH:FRESH] Fresh cache entry not found for node: {}", nodeId);
        }
    }

    return results;
}

std::vector<ReadResult> ReadStrategy::processStaleNodes(const std::vector<std::string>& nodeIds) {
    std::vector<ReadResult> results;
    results.reserve(nodeIds.size());

    spdlog::info("[CACHE_PATH:STALE_BATCH] Processing {} stale nodes (3-10s), returning cached values and scheduling background updates", nodeIds.size());

    // Return cached values
    for (const auto& nodeId : nodeIds) {
        auto cacheEntry = cacheManager_->getCachedValue(nodeId);
        if (cacheEntry.has_value()) {
            results.push_back(cacheEntry->toReadResult());
            spdlog::debug("[CACHE_PATH:STALE] Returned stale cached value for node: {}", nodeId);
        } else {
            results.push_back(createErrorResult(nodeId, "Stale cache entry not found"));
            spdlog::warn("[CACHE_PATH:STALE] Stale cache entry not found for node: {}", nodeId);
        }
    }

    // Schedule background updates for all stale nodes (non-blocking)
    scheduleBackgroundUpdates(nodeIds);
    spdlog::debug("[CACHE_PATH:STALE_BATCH] Background updates scheduled for {} nodes", nodeIds.size());

    return results;
}

std::vector<ReadResult> ReadStrategy::processExpiredNodes(const std::vector<std::string>& nodeIds) {
    if (nodeIds.empty()) {
        return {};
    }

    spdlog::info("[CACHE_PATH:EXPIRED_BATCH] Processing {} expired/missing nodes (> 10s or no cache), reading synchronously from OPC UA server", nodeIds.size());

    // Use intelligent batching if enabled
    if (intelligentBatchingEnabled_.load() && nodeIds.size() > optimalBatchSize_.load()) {
        return processExpiredNodesWithBatching(nodeIds);
    }

    // Otherwise use standard read and update
    return readAndUpdateCache(nodeIds);
}

std::vector<ReadResult> ReadStrategy::readAndUpdateCache(const std::vector<std::string>& nodeIds) {
    std::vector<ReadResult> results;

    try {
        spdlog::debug("[CACHE_PATH:EXPIRED/MISS] Reading {} nodes from OPC UA server", nodeIds.size());

        // Use batch read if available, otherwise read individually
        if (nodeIds.size() > 1) {
            results = opcClient_->readNodes(nodeIds);
        } else if (nodeIds.size() == 1) {
            results.push_back(opcClient_->readNode(nodeIds[0]));
        }

        // Update cache with results
        if (!results.empty()) {
            cacheManager_->updateCacheBatch(results);
            spdlog::debug("[CACHE_PATH:EXPIRED/MISS] Updated cache with {} read results", results.size());
        }

        // If error handler is available, handle partial batch failures
        if (errorHandler_ && !results.empty()) {
            results = errorHandler_->handlePartialBatchFailure(nodeIds, results);
        }

    } catch (const std::exception& e) {
        spdlog::error("[CACHE_PATH:EXPIRED/MISS] Error reading nodes from OPC UA server: {}", e.what());

        // If error handler is available, try cache fallback
        if (errorHandler_) {
            results.clear();
            results.reserve(nodeIds.size());

            for (const auto& nodeId : nodeIds) {
                auto cachedData = cacheManager_->getCachedValue(nodeId);
                ReadResult result = errorHandler_->handleConnectionError(nodeId, cachedData);
                results.push_back(result);
                if (cachedData.has_value()) {
                    spdlog::info("[CACHE_PATH:EXPIRED/MISS] Using cached fallback data for node {}", nodeId);
                }
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

void ReadStrategy::setOptimalBatchSize(size_t batchSize) {
    optimalBatchSize_.store(batchSize);
    spdlog::info("Optimal batch size set to {}", batchSize);
}

size_t ReadStrategy::getOptimalBatchSize() const {
    return optimalBatchSize_.load();
}

void ReadStrategy::setIntelligentBatchingEnabled(bool enabled) {
    intelligentBatchingEnabled_.store(enabled);
    spdlog::info("Intelligent batching {}", enabled ? "enabled" : "disabled");
}

bool ReadStrategy::isIntelligentBatchingEnabled() const {
    return intelligentBatchingEnabled_.load();
}

std::vector<std::vector<std::string>> ReadStrategy::splitIntoOptimalBatches(
    const std::vector<std::string>& nodeIds) {

    std::vector<std::vector<std::string>> batches;

    if (nodeIds.empty()) {
        return batches;
    }

    size_t batchSize = optimalBatchSize_.load();

    // If intelligent batching is disabled or batch size is 0, return single batch
    if (!intelligentBatchingEnabled_.load() || batchSize == 0) {
        batches.push_back(nodeIds);
        return batches;
    }

    // Split into batches of optimal size
    for (size_t i = 0; i < nodeIds.size(); i += batchSize) {
        size_t end = std::min(i + batchSize, nodeIds.size());
        std::vector<std::string> batch(nodeIds.begin() + i, nodeIds.begin() + end);
        batches.push_back(batch);
    }

    spdlog::debug("Split {} nodes into {} batches of size ~{}",
                  nodeIds.size(), batches.size(), batchSize);

    return batches;
}

std::vector<ReadResult> ReadStrategy::processExpiredNodesWithBatching(
    const std::vector<std::string>& nodeIds) {

    if (nodeIds.empty()) {
        return {};
    }

    std::vector<ReadResult> allResults;
    allResults.reserve(nodeIds.size());

    // Split into optimal batches
    auto batches = splitIntoOptimalBatches(nodeIds);

    spdlog::info("[CACHE_PATH:EXPIRED_BATCH] Processing {} expired/missing nodes in {} batches",
                  nodeIds.size(), batches.size());

    // Process each batch
    for (size_t i = 0; i < batches.size(); ++i) {
        const auto& batch = batches[i];
        try {
            spdlog::debug("[CACHE_PATH:EXPIRED_BATCH] Reading batch {}/{} with {} nodes from OPC UA server",
                         i + 1, batches.size(), batch.size());

            std::vector<ReadResult> batchResults;

            // Use batch read for multiple nodes
            if (batch.size() > 1) {
                batchResults = opcClient_->readNodesBatch(batch);
            } else if (batch.size() == 1) {
                batchResults.push_back(opcClient_->readNode(batch[0]));
            }

            // Update cache with batch results
            if (!batchResults.empty()) {
                cacheManager_->updateCacheBatch(batchResults);
                spdlog::debug("[CACHE_PATH:EXPIRED_BATCH] Updated cache with {} batch results", batchResults.size());
            }

            // Add to overall results
            allResults.insert(allResults.end(), batchResults.begin(), batchResults.end());

        } catch (const std::exception& e) {
            spdlog::error("[CACHE_PATH:EXPIRED_BATCH] Error processing batch {}/{}: {}", i + 1, batches.size(), e.what());

            // Handle batch failure with error handler or create error results
            if (errorHandler_) {
                for (const auto& nodeId : batch) {
                    auto cachedData = cacheManager_->getCachedValue(nodeId);
                    allResults.push_back(errorHandler_->handleConnectionError(nodeId, cachedData));
                    if (cachedData.has_value()) {
                        spdlog::info("[CACHE_PATH:EXPIRED_BATCH] Using cached fallback data for node {}", nodeId);
                    }
                }
            } else {
                for (const auto& nodeId : batch) {
                    allResults.push_back(createErrorResult(nodeId,
                        std::string("Batch read error: ") + e.what()));
                }
            }
        }
    }

    return allResults;
}

} // namespace opcua2http
