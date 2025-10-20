#pragma once

#include <chrono>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace opcua2http {

/**
 * @brief Performance monitor for cache operations and concurrency metrics
 *
 * This class tracks performance metrics including response times, lock contention,
 * wait times, and provides performance tuning recommendations.
 */
class PerformanceMonitor {
public:
    /**
     * @brief Operation type enumeration
     */
    enum class OperationType {
        CACHE_READ,
        CACHE_WRITE,
        CACHE_BATCH_READ,
        CACHE_BATCH_WRITE,
        OPC_READ,
        OPC_BATCH_READ,
        BACKGROUND_UPDATE
    };

    /**
     * @brief Performance metrics structure
     */
    struct PerformanceMetrics {
        // Response time metrics
        double avgCacheReadTime;        // Average cache read time (ms)
        double avgCacheWriteTime;       // Average cache write time (ms)
        double avgOPCReadTime;          // Average OPC UA read time (ms)
        double avgBatchReadTime;        // Average batch read time (ms)
        double avgBackgroundUpdateTime; // Average background update time (ms)

        // Concurrency metrics
        uint64_t totalLockWaits;        // Total lock wait events
        double avgLockWaitTime;         // Average lock wait time (ms)
        uint64_t lockContentions;       // Number of lock contentions
        double lockContentionRatio;     // Lock contention ratio (0.0 to 1.0)

        // Throughput metrics
        uint64_t operationsPerSecond;   // Operations per second
        uint64_t totalOperations;       // Total operations

        // Performance indicators
        bool isPerformanceGood;         // Overall performance indicator
        std::vector<std::string> recommendations; // Performance tuning recommendations
    };

    /**
     * @brief Operation timing structure for tracking individual operations
     */
    struct OperationTiming {
        OperationType type;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point endTime;
        double durationMs;
        bool completed;
    };

    /**
     * @brief Constructor
     */
    PerformanceMonitor();

    /**
     * @brief Destructor
     */
    ~PerformanceMonitor() = default;

    // Disable copy constructor and assignment operator
    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

    /**
     * @brief Start timing an operation
     * @param type Operation type
     * @return Operation ID for later completion
     */
    uint64_t startOperation(OperationType type);

    /**
     * @brief Complete timing an operation
     * @param operationId Operation ID from startOperation
     */
    void completeOperation(uint64_t operationId);

    /**
     * @brief Record operation time directly
     * @param type Operation type
     * @param durationMs Duration in milliseconds
     */
    void recordOperationTime(OperationType type, double durationMs);

    /**
     * @brief Record lock wait event
     * @param waitTimeMs Wait time in milliseconds
     */
    void recordLockWait(double waitTimeMs);

    /**
     * @brief Record lock contention event
     */
    void recordLockContention();

    /**
     * @brief Get performance metrics
     * @return PerformanceMetrics structure with current metrics
     */
    PerformanceMetrics getMetrics() const;

    /**
     * @brief Get performance recommendations based on current metrics
     * @return Vector of recommendation strings
     */
    std::vector<std::string> getRecommendations() const;

    /**
     * @brief Reset all performance metrics
     */
    void reset();

    /**
     * @brief Enable or disable performance monitoring
     * @param enabled Whether monitoring should be enabled
     */
    void setEnabled(bool enabled);

    /**
     * @brief Check if performance monitoring is enabled
     * @return True if monitoring is enabled
     */
    bool isEnabled() const;

    /**
     * @brief Set performance threshold for "good" performance
     * @param cacheReadThresholdMs Cache read threshold in ms (default: 1.0)
     * @param opcReadThresholdMs OPC read threshold in ms (default: 100.0)
     */
    void setPerformanceThresholds(double cacheReadThresholdMs, double opcReadThresholdMs);

private:
    // Configuration
    std::atomic<bool> enabled_{true};
    std::atomic<double> cacheReadThreshold_{1.0};   // 1ms threshold for cache reads
    std::atomic<double> opcReadThreshold_{100.0};   // 100ms threshold for OPC reads

    // Operation tracking
    std::atomic<uint64_t> nextOperationId_{0};
    mutable std::mutex operationsMutex_;
    std::unordered_map<uint64_t, OperationTiming> activeOperations_;

    // Timing statistics (atomic for lock-free updates)
    std::atomic<uint64_t> cacheReadCount_{0};
    std::atomic<double> cacheReadTotalTime_{0.0};
    std::atomic<uint64_t> cacheWriteCount_{0};
    std::atomic<double> cacheWriteTotalTime_{0.0};
    std::atomic<uint64_t> opcReadCount_{0};
    std::atomic<double> opcReadTotalTime_{0.0};
    std::atomic<uint64_t> batchReadCount_{0};
    std::atomic<double> batchReadTotalTime_{0.0};
    std::atomic<uint64_t> backgroundUpdateCount_{0};
    std::atomic<double> backgroundUpdateTotalTime_{0.0};

    // Concurrency statistics
    std::atomic<uint64_t> lockWaitCount_{0};
    std::atomic<double> lockWaitTotalTime_{0.0};
    std::atomic<uint64_t> lockContentionCount_{0};
    std::atomic<uint64_t> lockAcquireAttempts_{0};

    // Throughput tracking
    std::chrono::steady_clock::time_point startTime_;
    std::atomic<uint64_t> totalOperations_{0};

    /**
     * @brief Update operation statistics
     * @param type Operation type
     * @param durationMs Duration in milliseconds
     */
    void updateStatistics(OperationType type, double durationMs);

    /**
     * @brief Calculate average time for operation type
     * @param totalTime Total time accumulated
     * @param count Number of operations
     * @return Average time in milliseconds
     */
    double calculateAverage(double totalTime, uint64_t count) const;

    /**
     * @brief Analyze performance and generate recommendations
     * @param metrics Current performance metrics
     * @return Vector of recommendation strings
     */
    std::vector<std::string> analyzePerformance(const PerformanceMetrics& metrics) const;
};

} // namespace opcua2http
