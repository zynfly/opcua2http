#include "cache/CacheMemoryManager.h"
#include <algorithm>
#include <iostream>

namespace opcua2http {

CacheMemoryManager::CacheMemoryManager(size_t maxMemoryBytes, size_t maxEntries)
    : maxMemoryBytes_(maxMemoryBytes)
    , maxEntries_(maxEntries)
    , lastEviction_(std::chrono::steady_clock::now()) {

    std::cout << "CacheMemoryManager initialized with max memory: "
              << (maxMemoryBytes / (1024 * 1024)) << " MB, max entries: "
              << maxEntries << std::endl;
}

void CacheMemoryManager::setMaxMemoryUsage(size_t maxBytes) {
    maxMemoryBytes_.store(maxBytes, std::memory_order_relaxed);
    std::cout << "Max memory usage set to " << (maxBytes / (1024 * 1024)) << " MB" << std::endl;
}

void CacheMemoryManager::setMaxEntries(size_t maxEntries) {
    maxEntries_.store(maxEntries, std::memory_order_relaxed);
    std::cout << "Max entries set to " << maxEntries << std::endl;
}

size_t CacheMemoryManager::getMaxMemoryUsage() const {
    return maxMemoryBytes_.load(std::memory_order_relaxed);
}

size_t CacheMemoryManager::getMaxEntries() const {
    return maxEntries_.load(std::memory_order_relaxed);
}

void CacheMemoryManager::updateCurrentMemoryUsage(size_t memoryBytes) {
    currentMemoryBytes_.store(memoryBytes, std::memory_order_relaxed);
}

void CacheMemoryManager::updateCurrentEntryCount(size_t entryCount) {
    currentEntries_.store(entryCount, std::memory_order_relaxed);
}

size_t CacheMemoryManager::getCurrentMemoryUsage() const {
    return currentMemoryBytes_.load(std::memory_order_relaxed);
}

size_t CacheMemoryManager::getCurrentEntryCount() const {
    return currentEntries_.load(std::memory_order_relaxed);
}

double CacheMemoryManager::getMemoryUsageRatio() const {
    size_t current = currentMemoryBytes_.load(std::memory_order_relaxed);
    size_t max = maxMemoryBytes_.load(std::memory_order_relaxed);

    if (max == 0) {
        return 0.0;
    }

    return static_cast<double>(current) / max;
}

double CacheMemoryManager::getEntryUsageRatio() const {
    size_t current = currentEntries_.load(std::memory_order_relaxed);
    size_t max = maxEntries_.load(std::memory_order_relaxed);

    if (max == 0) {
        return 0.0;
    }

    return static_cast<double>(current) / max;
}

bool CacheMemoryManager::hasMemoryPressure(double threshold) const {
    if (!enabled_.load(std::memory_order_relaxed)) {
        return false;
    }

    return getMemoryUsageRatio() >= threshold;
}

bool CacheMemoryManager::hasEntryPressure(double threshold) const {
    if (!enabled_.load(std::memory_order_relaxed)) {
        return false;
    }

    return getEntryUsageRatio() >= threshold;
}

size_t CacheMemoryManager::calculateEvictionCount(double targetRatio) const {
    size_t current = currentEntries_.load(std::memory_order_relaxed);
    size_t max = maxEntries_.load(std::memory_order_relaxed);

    if (current == 0 || max == 0) {
        return 0;
    }

    double currentRatio = getMemoryUsageRatio();
    if (currentRatio <= targetRatio) {
        return 0;
    }

    // Calculate how many entries to remove to reach target ratio
    // Assuming uniform entry sizes for simplicity
    size_t targetEntries = static_cast<size_t>(max * targetRatio);

    if (current <= targetEntries) {
        return 0;
    }

    return current - targetEntries;
}

void CacheMemoryManager::recordEviction(size_t count, const std::string& reason) {
    totalEvictions_.fetch_add(count, std::memory_order_relaxed);

    if (reason == "lru") {
        lruEvictions_.fetch_add(count, std::memory_order_relaxed);
    } else if (reason == "memory_pressure") {
        memoryPressureEvictions_.fetch_add(count, std::memory_order_relaxed);
    }

    lastEviction_ = std::chrono::steady_clock::now();

    std::cout << "Recorded " << count << " evictions (reason: " << reason << ")" << std::endl;
}

void CacheMemoryManager::setEvictionCallback(EvictionCallback callback) {
    evictionCallback_ = std::move(callback);
}

void CacheMemoryManager::triggerEvictionCallback(const std::string& nodeId, const std::string& reason) {
    if (evictionCallback_) {
        evictionCallback_(nodeId, reason);
    }
}

CacheMemoryManager::MemoryStats CacheMemoryManager::getStats() const {
    return MemoryStats{
        currentMemoryBytes_.load(std::memory_order_relaxed),
        maxMemoryBytes_.load(std::memory_order_relaxed),
        currentEntries_.load(std::memory_order_relaxed),
        maxEntries_.load(std::memory_order_relaxed),
        getMemoryUsageRatio(),
        getEntryUsageRatio(),
        totalEvictions_.load(std::memory_order_relaxed),
        lruEvictions_.load(std::memory_order_relaxed),
        memoryPressureEvictions_.load(std::memory_order_relaxed),
        lastEviction_
    };
}

void CacheMemoryManager::resetStats() {
    totalEvictions_.store(0, std::memory_order_relaxed);
    lruEvictions_.store(0, std::memory_order_relaxed);
    memoryPressureEvictions_.store(0, std::memory_order_relaxed);
    lastEviction_ = std::chrono::steady_clock::now();

    std::cout << "Memory manager statistics reset" << std::endl;
}

void CacheMemoryManager::setEnabled(bool enabled) {
    enabled_.store(enabled, std::memory_order_relaxed);
    std::cout << "Memory management " << (enabled ? "enabled" : "disabled") << std::endl;
}

bool CacheMemoryManager::isEnabled() const {
    return enabled_.load(std::memory_order_relaxed);
}

} // namespace opcua2http
