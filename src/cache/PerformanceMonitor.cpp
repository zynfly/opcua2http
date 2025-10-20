#include "cache/PerformanceMonitor.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace opcua2http {

PerformanceMonitor::PerformanceMonitor()
    : startTime_(std::chrono::steady_clock::now()) {
    spdlog::debug("PerformanceMonitor initialized");
}

uint64_t PerformanceMonitor::startOperation(OperationType type) {
    if (!enabled_.load(std::memory_order_relaxed)) {
        return 0;
    }

    uint64_t operationId = nextOperationId_.fetch_add(1, std::memory_order_relaxed);

    OperationTiming timing;
    timing.type = type;
    timing.startTime = std::chrono::steady_clock::now();
    timing.completed = false;

    std::lock_guard<std::mutex> lock(operationsMutex_);
    activeOperations_[operationId] = timing;

    return operationId;
}

void PerformanceMonitor::completeOperation(uint64_t operationId) {
    if (!enabled_.load(std::memory_order_relaxed) || operationId == 0) {
        return;
    }

    auto endTime = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(operationsMutex_);

    auto it = activeOperations_.find(operationId);
    if (it != activeOperations_.end()) {
        auto& timing = it->second;
        timing.endTime = endTime;
        timing.completed = true;

        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            timing.endTime - timing.startTime);
        timing.durationMs = duration.count() / 1000.0;

        // Update statistics
        updateStatistics(timing.type, timing.durationMs);

        // Remove from active operations
        activeOperations_.erase(it);
    }
}

void PerformanceMonitor::recordOperationTime(OperationType type, double durationMs) {
    if (!enabled_.load(std::memory_order_relaxed)) {
        return;
    }

    updateStatistics(type, durationMs);
}

void PerformanceMonitor::recordLockWait(double waitTimeMs) {
    if (!enabled_.load(std::memory_order_relaxed)) {
        return;
    }

    lockWaitCount_.fetch_add(1, std::memory_order_relaxed);

    double currentTotal = lockWaitTotalTime_.load(std::memory_order_relaxed);
    lockWaitTotalTime_.store(currentTotal + waitTimeMs, std::memory_order_relaxed);

    spdlog::debug("Lock wait recorded: {:.3f} ms", waitTimeMs);
}

void PerformanceMonitor::recordLockContention() {
    if (!enabled_.load(std::memory_order_relaxed)) {
        return;
    }

    lockContentionCount_.fetch_add(1, std::memory_order_relaxed);
    lockAcquireAttempts_.fetch_add(1, std::memory_order_relaxed);
}

PerformanceMonitor::PerformanceMetrics PerformanceMonitor::getMetrics() const {
    PerformanceMetrics metrics;

    // Calculate average response times
    metrics.avgCacheReadTime = calculateAverage(
        cacheReadTotalTime_.load(std::memory_order_relaxed),
        cacheReadCount_.load(std::memory_order_relaxed));

    metrics.avgCacheWriteTime = calculateAverage(
        cacheWriteTotalTime_.load(std::memory_order_relaxed),
        cacheWriteCount_.load(std::memory_order_relaxed));

    metrics.avgOPCReadTime = calculateAverage(
        opcReadTotalTime_.load(std::memory_order_relaxed),
        opcReadCount_.load(std::memory_order_relaxed));

    metrics.avgBatchReadTime = calculateAverage(
        batchReadTotalTime_.load(std::memory_order_relaxed),
        batchReadCount_.load(std::memory_order_relaxed));

    metrics.avgBackgroundUpdateTime = calculateAverage(
        backgroundUpdateTotalTime_.load(std::memory_order_relaxed),
        backgroundUpdateCount_.load(std::memory_order_relaxed));

    // Calculate concurrency metrics
    metrics.totalLockWaits = lockWaitCount_.load(std::memory_order_relaxed);
    metrics.avgLockWaitTime = calculateAverage(
        lockWaitTotalTime_.load(std::memory_order_relaxed),
        lockWaitCount_.load(std::memory_order_relaxed));

    metrics.lockContentions = lockContentionCount_.load(std::memory_order_relaxed);

    uint64_t attempts = lockAcquireAttempts_.load(std::memory_order_relaxed);
    metrics.lockContentionRatio = (attempts > 0)
        ? static_cast<double>(metrics.lockContentions) / attempts
        : 0.0;

    // Calculate throughput
    metrics.totalOperations = totalOperations_.load(std::memory_order_relaxed);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_);
    metrics.operationsPerSecond = (elapsed.count() > 0)
        ? metrics.totalOperations / elapsed.count()
        : 0;

    // Determine overall performance
    double cacheThreshold = cacheReadThreshold_.load(std::memory_order_relaxed);
    double opcThreshold = opcReadThreshold_.load(std::memory_order_relaxed);

    metrics.isPerformanceGood =
        (metrics.avgCacheReadTime <= cacheThreshold) &&
        (metrics.avgOPCReadTime <= opcThreshold) &&
        (metrics.lockContentionRatio < 0.1); // Less than 10% contention

    // Generate recommendations
    metrics.recommendations = analyzePerformance(metrics);

    return metrics;
}

std::vector<std::string> PerformanceMonitor::getRecommendations() const {
    auto metrics = getMetrics();
    return metrics.recommendations;
}

void PerformanceMonitor::reset() {
    std::lock_guard<std::mutex> lock(operationsMutex_);

    // Clear active operations
    activeOperations_.clear();

    // Reset all counters
    cacheReadCount_.store(0, std::memory_order_relaxed);
    cacheReadTotalTime_.store(0.0, std::memory_order_relaxed);
    cacheWriteCount_.store(0, std::memory_order_relaxed);
    cacheWriteTotalTime_.store(0.0, std::memory_order_relaxed);
    opcReadCount_.store(0, std::memory_order_relaxed);
    opcReadTotalTime_.store(0.0, std::memory_order_relaxed);
    batchReadCount_.store(0, std::memory_order_relaxed);
    batchReadTotalTime_.store(0.0, std::memory_order_relaxed);
    backgroundUpdateCount_.store(0, std::memory_order_relaxed);
    backgroundUpdateTotalTime_.store(0.0, std::memory_order_relaxed);
    lockWaitCount_.store(0, std::memory_order_relaxed);
    lockWaitTotalTime_.store(0.0, std::memory_order_relaxed);
    lockContentionCount_.store(0, std::memory_order_relaxed);
    lockAcquireAttempts_.store(0, std::memory_order_relaxed);
    totalOperations_.store(0, std::memory_order_relaxed);

    // Reset start time
    startTime_ = std::chrono::steady_clock::now();

    spdlog::info("Performance monitor statistics reset");
}

void PerformanceMonitor::setEnabled(bool enabled) {
    enabled_.store(enabled, std::memory_order_relaxed);
    spdlog::info("Performance monitoring {}", enabled ? "enabled" : "disabled");
}

bool PerformanceMonitor::isEnabled() const {
    return enabled_.load(std::memory_order_relaxed);
}

void PerformanceMonitor::setPerformanceThresholds(double cacheReadThresholdMs,
                                                  double opcReadThresholdMs) {
    cacheReadThreshold_.store(cacheReadThresholdMs, std::memory_order_relaxed);
    opcReadThreshold_.store(opcReadThresholdMs, std::memory_order_relaxed);

    spdlog::info("Performance thresholds set: cache read = {:.3f} ms, OPC read = {:.3f} ms",
                 cacheReadThresholdMs, opcReadThresholdMs);
}

void PerformanceMonitor::updateStatistics(OperationType type, double durationMs) {
    totalOperations_.fetch_add(1, std::memory_order_relaxed);

    switch (type) {
        case OperationType::CACHE_READ:
        case OperationType::CACHE_BATCH_READ: {
            cacheReadCount_.fetch_add(1, std::memory_order_relaxed);
            double currentTotal = cacheReadTotalTime_.load(std::memory_order_relaxed);
            cacheReadTotalTime_.store(currentTotal + durationMs, std::memory_order_relaxed);
            break;
        }
        case OperationType::CACHE_WRITE:
        case OperationType::CACHE_BATCH_WRITE: {
            cacheWriteCount_.fetch_add(1, std::memory_order_relaxed);
            double currentTotal = cacheWriteTotalTime_.load(std::memory_order_relaxed);
            cacheWriteTotalTime_.store(currentTotal + durationMs, std::memory_order_relaxed);
            break;
        }
        case OperationType::OPC_READ: {
            opcReadCount_.fetch_add(1, std::memory_order_relaxed);
            double currentTotal = opcReadTotalTime_.load(std::memory_order_relaxed);
            opcReadTotalTime_.store(currentTotal + durationMs, std::memory_order_relaxed);
            break;
        }
        case OperationType::OPC_BATCH_READ: {
            batchReadCount_.fetch_add(1, std::memory_order_relaxed);
            double currentTotal = batchReadTotalTime_.load(std::memory_order_relaxed);
            batchReadTotalTime_.store(currentTotal + durationMs, std::memory_order_relaxed);
            break;
        }
        case OperationType::BACKGROUND_UPDATE: {
            backgroundUpdateCount_.fetch_add(1, std::memory_order_relaxed);
            double currentTotal = backgroundUpdateTotalTime_.load(std::memory_order_relaxed);
            backgroundUpdateTotalTime_.store(currentTotal + durationMs, std::memory_order_relaxed);
            break;
        }
    }
}

double PerformanceMonitor::calculateAverage(double totalTime, uint64_t count) const {
    return (count > 0) ? (totalTime / count) : 0.0;
}

std::vector<std::string> PerformanceMonitor::analyzePerformance(
    const PerformanceMetrics& metrics) const {

    std::vector<std::string> recommendations;

    // Check cache read performance
    double cacheThreshold = cacheReadThreshold_.load(std::memory_order_relaxed);
    if (metrics.avgCacheReadTime > cacheThreshold) {
        recommendations.push_back(
            "Cache read time (" + std::to_string(metrics.avgCacheReadTime) +
            " ms) exceeds threshold (" + std::to_string(cacheThreshold) +
            " ms). Consider: 1) Reducing cache size, 2) Optimizing data structures, " +
            "3) Increasing memory limits");
    }

    // Check OPC read performance
    double opcThreshold = opcReadThreshold_.load(std::memory_order_relaxed);
    if (metrics.avgOPCReadTime > opcThreshold) {
        recommendations.push_back(
            "OPC UA read time (" + std::to_string(metrics.avgOPCReadTime) +
            " ms) exceeds threshold (" + std::to_string(opcThreshold) +
            " ms). Consider: 1) Increasing batch size, 2) Checking network latency, " +
            "3) Optimizing OPC UA server configuration");
    }

    // Check lock contention
    if (metrics.lockContentionRatio > 0.1) {
        recommendations.push_back(
            "High lock contention detected (" +
            std::to_string(metrics.lockContentionRatio * 100) +
            "%). Consider: 1) Increasing cache refresh threshold, " +
            "2) Enabling intelligent batching, 3) Reducing concurrent operations");
    }

    // Check lock wait times
    if (metrics.avgLockWaitTime > 5.0) {
        recommendations.push_back(
            "High average lock wait time (" + std::to_string(metrics.avgLockWaitTime) +
            " ms). Consider: 1) Optimizing critical sections, " +
            "2) Using finer-grained locking, 3) Reducing lock hold times");
    }

    // Check batch read efficiency
    if (metrics.avgBatchReadTime > 0 && metrics.avgOPCReadTime > 0) {
        double efficiency = metrics.avgBatchReadTime / metrics.avgOPCReadTime;
        if (efficiency > 2.0) {
            recommendations.push_back(
                "Batch reads are not efficient (ratio: " + std::to_string(efficiency) +
                "). Consider: 1) Adjusting optimal batch size, " +
                "2) Checking OPC UA server batch read support");
        }
    }

    // Check throughput
    if (metrics.operationsPerSecond < 10 && metrics.totalOperations > 100) {
        recommendations.push_back(
            "Low throughput detected (" + std::to_string(metrics.operationsPerSecond) +
            " ops/sec). Consider: 1) Increasing cache expire time, " +
            "2) Enabling background updates, 3) Optimizing cache strategy");
    }

    // If no issues found
    if (recommendations.empty()) {
        recommendations.push_back("Performance is within acceptable thresholds. No tuning needed.");
    }

    return recommendations;
}

} // namespace opcua2http
