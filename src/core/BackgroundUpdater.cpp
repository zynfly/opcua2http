#include "core/BackgroundUpdater.h"
#include "cache/CacheManager.h"
#include "opcua/OPCUAClient.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace opcua2http {

BackgroundUpdater::BackgroundUpdater(CacheManager* cacheManager, OPCUAClient* opcClient)
    : cacheManager_(cacheManager)
    , opcClient_(opcClient)
    , lastUpdate_(std::chrono::steady_clock::now()) {
    
    if (!cacheManager_) {
        throw std::invalid_argument("CacheManager cannot be null");
    }
    if (!opcClient_) {
        throw std::invalid_argument("OPCUAClient cannot be null");
    }
    
    spdlog::debug("BackgroundUpdater created with cache manager and OPC client");
}

BackgroundUpdater::~BackgroundUpdater() {
    stop();
    spdlog::debug("BackgroundUpdater destroyed");
}

void BackgroundUpdater::scheduleUpdate(const std::string& nodeId) {
    if (nodeId.empty()) {
        spdlog::warn("BackgroundUpdater::scheduleUpdate called with empty nodeId");
        return;
    }

    if (!running_.load()) {
        spdlog::debug("BackgroundUpdater not running, ignoring update request for node: {}", nodeId);
        return;
    }

    // Check for duplicates first
    if (!addToPendingUpdates(nodeId)) {
        duplicateUpdates_.fetch_add(1, std::memory_order_relaxed);
        spdlog::trace("Duplicate update request filtered for node: {}", nodeId);
        return;
    }

    // Add to queue if not full
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        
        if (isQueueFull()) {
            spdlog::warn("Update queue is full, dropping update request for node: {}", nodeId);
            removeFromPendingUpdates(nodeId);
            return;
        }
        
        updateQueue_.push(nodeId);
        spdlog::trace("Scheduled background update for node: {}", nodeId);
    }
    
    queueCondition_.notify_one();
}

void BackgroundUpdater::scheduleBatchUpdate(const std::vector<std::string>& nodeIds) {
    if (nodeIds.empty()) {
        spdlog::debug("BackgroundUpdater::scheduleBatchUpdate called with empty nodeIds vector");
        return;
    }

    if (!running_.load()) {
        spdlog::debug("BackgroundUpdater not running, ignoring batch update request for {} nodes", nodeIds.size());
        return;
    }

    size_t scheduled = 0;
    size_t duplicates = 0;
    size_t dropped = 0;

    for (const auto& nodeId : nodeIds) {
        if (nodeId.empty()) {
            continue;
        }

        // Check for duplicates
        if (!addToPendingUpdates(nodeId)) {
            duplicates++;
            continue;
        }

        // Add to queue if not full
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            
            if (isQueueFull()) {
                dropped++;
                removeFromPendingUpdates(nodeId);
                continue;
            }
            
            updateQueue_.push(nodeId);
            scheduled++;
        }
    }

    // Update statistics
    duplicateUpdates_.fetch_add(duplicates, std::memory_order_relaxed);
    
    if (scheduled > 0) {
        queueCondition_.notify_all();
        spdlog::debug("Scheduled {} background updates, {} duplicates filtered, {} dropped (queue full)", 
                     scheduled, duplicates, dropped);
    }
    
    if (dropped > 0) {
        spdlog::warn("Dropped {} update requests due to full queue", dropped);
    }
}

void BackgroundUpdater::start() {
    if (running_.load()) {
        spdlog::warn("BackgroundUpdater is already running");
        return;
    }

    stopRequested_.store(false);
    running_.store(true);

    // Create worker threads
    size_t numThreads = maxConcurrentUpdates_.load();
    workerThreads_.reserve(numThreads);
    
    for (size_t i = 0; i < numThreads; ++i) {
        workerThreads_.emplace_back(&BackgroundUpdater::workerLoop, this);
    }

    spdlog::info("BackgroundUpdater started with {} worker threads", numThreads);
}

void BackgroundUpdater::stop() {
    if (!running_.load()) {
        return;
    }

    spdlog::info("Stopping BackgroundUpdater...");
    
    // Signal stop to all threads
    stopRequested_.store(true);
    running_.store(false);
    
    // Wake up all waiting threads
    queueCondition_.notify_all();
    
    // Wait for all worker threads to finish
    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    workerThreads_.clear();
    
    // Clear remaining queue items
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        while (!updateQueue_.empty()) {
            updateQueue_.pop();
        }
    }
    
    // Clear pending updates
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingUpdates_.clear();
    }

    spdlog::info("BackgroundUpdater stopped");
}

bool BackgroundUpdater::isRunning() const {
    return running_.load();
}

void BackgroundUpdater::setMaxConcurrentUpdates(size_t maxUpdates) {
    if (maxUpdates == 0) {
        spdlog::warn("Invalid maxConcurrentUpdates value: 0, using default: 3");
        maxUpdates = 3;
    }
    
    maxConcurrentUpdates_.store(maxUpdates);
    spdlog::debug("Set maxConcurrentUpdates to: {}", maxUpdates);
}

void BackgroundUpdater::setUpdateQueueSize(size_t maxQueueSize) {
    if (maxQueueSize == 0) {
        spdlog::warn("Invalid updateQueueSize value: 0, using default: 1000");
        maxQueueSize = 1000;
    }
    
    maxQueueSize_.store(maxQueueSize);
    spdlog::debug("Set updateQueueSize to: {}", maxQueueSize);
}

void BackgroundUpdater::setUpdateTimeout(std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0) {
        spdlog::warn("Invalid updateTimeout value: {}ms, using default: 5000ms", timeout.count());
        timeout = std::chrono::milliseconds(5000);
    }
    
    updateTimeout_.store(timeout);
    spdlog::debug("Set updateTimeout to: {}ms", timeout.count());
}

BackgroundUpdater::UpdateStats BackgroundUpdater::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    UpdateStats stats;
    stats.totalUpdates = totalUpdates_.load();
    stats.successfulUpdates = successfulUpdates_.load();
    stats.failedUpdates = failedUpdates_.load();
    stats.duplicateUpdates = duplicateUpdates_.load();
    stats.queuedUpdates = getQueueSize();
    stats.lastUpdate = lastUpdate_;
    
    // Calculate average update time
    uint64_t total = stats.totalUpdates;
    if (total > 0) {
        stats.averageUpdateTime = totalUpdateTime_.load() / total;
    }
    
    return stats;
}

void BackgroundUpdater::clearStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    totalUpdates_.store(0);
    successfulUpdates_.store(0);
    failedUpdates_.store(0);
    duplicateUpdates_.store(0);
    totalUpdateTime_.store(0.0);
    lastUpdate_ = std::chrono::steady_clock::now();
    
    spdlog::debug("BackgroundUpdater statistics cleared");
}

void BackgroundUpdater::workerLoop() {
    spdlog::debug("BackgroundUpdater worker thread started");
    
    while (!stopRequested_.load()) {
        std::string nodeId = getNextUpdate();
        
        if (nodeId.empty()) {
            // Empty nodeId means we should stop
            break;
        }
        
        processUpdate(nodeId);
    }
    
    spdlog::debug("BackgroundUpdater worker thread finished");
}

void BackgroundUpdater::processUpdate(const std::string& nodeId) {
    auto startTime = std::chrono::steady_clock::now();
    bool success = false;
    
    try {
        spdlog::trace("Processing background update for node: {}", nodeId);
        
        // Read from OPC UA server
        ReadResult result = opcClient_->readNode(nodeId);
        
        if (result.success) {
            // Update cache with new data
            cacheManager_->updateCache(nodeId, result.value, "Good", result.reason, result.timestamp);
            success = true;
            spdlog::trace("Successfully updated cache for node: {} with value: {}", nodeId, result.value);
        } else {
            spdlog::debug("Failed to read node {} during background update: {}", nodeId, result.reason);
        }
        
    } catch (const std::exception& e) {
        spdlog::error("Exception during background update for node {}: {}", nodeId, e.what());
    } catch (...) {
        spdlog::error("Unknown exception during background update for node: {}", nodeId);
    }
    
    // Remove from pending updates
    removeFromPendingUpdates(nodeId);
    
    // Record statistics
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    double updateTimeMs = duration.count() / 1000.0;
    
    recordUpdateStats(success, updateTimeMs);
}

bool BackgroundUpdater::addToPendingUpdates(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    
    // Try to insert, returns pair<iterator, bool> where bool indicates if insertion took place
    auto result = pendingUpdates_.insert(nodeId);
    return result.second; // true if inserted (not duplicate), false if already exists
}

void BackgroundUpdater::removeFromPendingUpdates(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    pendingUpdates_.erase(nodeId);
}

std::string BackgroundUpdater::getNextUpdate() {
    std::unique_lock<std::mutex> lock(queueMutex_);
    
    // Wait for work or stop signal
    queueCondition_.wait(lock, [this] {
        return !updateQueue_.empty() || stopRequested_.load();
    });
    
    if (stopRequested_.load() && updateQueue_.empty()) {
        return ""; // Signal to stop
    }
    
    if (!updateQueue_.empty()) {
        std::string nodeId = updateQueue_.front();
        updateQueue_.pop();
        return nodeId;
    }
    
    return ""; // Should not reach here, but return empty to be safe
}

void BackgroundUpdater::recordUpdateStats(bool success, double updateTime) {
    totalUpdates_.fetch_add(1, std::memory_order_relaxed);
    
    if (success) {
        successfulUpdates_.fetch_add(1, std::memory_order_relaxed);
    } else {
        failedUpdates_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Update average time calculation
    double currentTotal = totalUpdateTime_.load();
    totalUpdateTime_.store(currentTotal + updateTime);
    
    // Update last update time
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        lastUpdate_ = std::chrono::steady_clock::now();
    }
}

bool BackgroundUpdater::isQueueFull() const {
    // This method should be called with queueMutex_ already locked
    return updateQueue_.size() >= maxQueueSize_.load();
}

size_t BackgroundUpdater::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return updateQueue_.size();
}

} // namespace opcua2http