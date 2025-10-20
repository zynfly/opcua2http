#pragma once

#include <string>
#include <optional>
#include <chrono>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "cache/CacheManager.h"
#include "core/ReadResult.h"

namespace opcua2http {

// Forward declarations
class OPCUAClient;

/**
 * @brief Error handler for cache-based OPC UA operations
 *
 * This class implements intelligent error handling and fallback strategies
 * for OPC UA connection failures and other error scenarios. It determines
 * the appropriate action based on error type, cache availability, and
 * system state.
 */
class CacheErrorHandler {
public:
    /**
     * @brief Error action enumeration
     */
    enum class ErrorAction {
        RETURN_CACHED,      // Return cached data (if available)
        RETURN_ERROR,       // Return error response to client
        RETRY_CONNECTION    // Attempt to retry the connection
    };

    /**
     * @brief Error statistics structure
     */
    struct ErrorStats {
        uint64_t totalErrors{0};                    // Total errors encountered
        uint64_t connectionErrors{0};               // OPC UA connection errors
        uint64_t cacheHitOnError{0};                // Errors with cache fallback
        uint64_t cacheMissOnError{0};               // Errors without cache fallback
        uint64_t retryAttempts{0};                  // Connection retry attempts
        uint64_t successfulRetries{0};              // Successful retries
        uint64_t failedRetries{0};                  // Failed retries
        std::chrono::steady_clock::time_point lastError;  // Last error timestamp
        double errorRate{0.0};                      // Current error rate (errors/minute)
    };

    /**
     * @brief Constructor
     * @param cacheManager Pointer to cache manager instance
     * @param opcClient Pointer to OPC UA client instance
     */
    CacheErrorHandler(CacheManager* cacheManager, OPCUAClient* opcClient);

    /**
     * @brief Destructor
     */
    ~CacheErrorHandler() = default;

    // Disable copy constructor and assignment operator
    CacheErrorHandler(const CacheErrorHandler&) = delete;
    CacheErrorHandler& operator=(const CacheErrorHandler&) = delete;

    /**
     * @brief Determine appropriate action for an error scenario
     * @param nodeId Node identifier that caused the error
     * @param error Exception or error that occurred
     * @param hasCachedData Whether cached data is available for this node
     * @return ErrorAction indicating what action to take
     */
    ErrorAction determineAction(const std::string& nodeId,
                               const std::string& error,
                               bool hasCachedData);

    /**
     * @brief Handle OPC UA connection error with intelligent fallback
     * @param nodeId Node identifier that failed to read
     * @param cachedData Optional cached data for fallback
     * @return ReadResult with cached data or error response
     */
    ReadResult handleConnectionError(const std::string& nodeId,
                                   const std::optional<CacheManager::CacheEntry>& cachedData);

    /**
     * @brief Handle partial batch failure (some nodes succeed, some fail)
     * @param nodeIds Vector of node identifiers in the batch
     * @param results Vector of results (may contain errors)
     * @return Vector of ReadResults with fallback applied where needed
     */
    std::vector<ReadResult> handlePartialBatchFailure(
        const std::vector<std::string>& nodeIds,
        const std::vector<ReadResult>& results);

    /**
     * @brief Check if error is a connection-related error
     * @param error Error message or exception text
     * @return True if error is connection-related
     */
    bool isConnectionError(const std::string& error) const;

    /**
     * @brief Check if error is a timeout error
     * @param error Error message or exception text
     * @return True if error is timeout-related
     */
    bool isTimeoutError(const std::string& error) const;

    /**
     * @brief Check if error is recoverable (retry may help)
     * @param error Error message or exception text
     * @return True if error is potentially recoverable
     */
    bool isRecoverableError(const std::string& error) const;

    /**
     * @brief Get error statistics
     * @return ErrorStats structure with current statistics
     */
    ErrorStats getStats() const;

    /**
     * @brief Reset error statistics
     */
    void resetStats();

    /**
     * @brief Set maximum retry attempts for connection errors
     * @param maxRetries Maximum number of retry attempts
     */
    void setMaxRetryAttempts(int maxRetries);

    /**
     * @brief Get maximum retry attempts setting
     * @return Maximum retry attempts
     */
    int getMaxRetryAttempts() const;

    /**
     * @brief Set retry delay duration
     * @param delay Delay between retry attempts
     */
    void setRetryDelay(std::chrono::milliseconds delay);

    /**
     * @brief Get retry delay setting
     * @return Retry delay duration
     */
    std::chrono::milliseconds getRetryDelay() const;

    /**
     * @brief Enable or disable automatic retry on connection errors
     * @param enabled Whether automatic retry should be enabled
     */
    void setAutoRetryEnabled(bool enabled);

    /**
     * @brief Check if automatic retry is enabled
     * @return True if automatic retry is enabled
     */
    bool isAutoRetryEnabled() const;

    /**
     * @brief Set error rate threshold for alerting
     * @param threshold Error rate threshold (errors per minute)
     */
    void setErrorRateThreshold(double threshold);

    /**
     * @brief Get error rate threshold
     * @return Error rate threshold
     */
    double getErrorRateThreshold() const;

    /**
     * @brief Check if error rate exceeds threshold
     * @return True if error rate is above threshold
     */
    bool isErrorRateExceeded() const;

private:
    // Dependencies
    CacheManager* cacheManager_;                              // Cache manager instance
    OPCUAClient* opcClient_;                                  // OPC UA client instance

    // Configuration
    std::atomic<int> maxRetryAttempts_{3};                   // Maximum retry attempts
    std::atomic<bool> autoRetryEnabled_{true};               // Automatic retry enabled
    std::chrono::milliseconds retryDelay_{1000};             // Delay between retries
    std::atomic<double> errorRateThreshold_{10.0};           // Error rate threshold (errors/min)

    // Statistics (atomic for thread-safe access)
    mutable std::atomic<uint64_t> totalErrors_{0};
    mutable std::atomic<uint64_t> connectionErrors_{0};
    mutable std::atomic<uint64_t> cacheHitOnError_{0};
    mutable std::atomic<uint64_t> cacheMissOnError_{0};
    mutable std::atomic<uint64_t> retryAttempts_{0};
    mutable std::atomic<uint64_t> successfulRetries_{0};
    mutable std::atomic<uint64_t> failedRetries_{0};
    mutable std::atomic<std::chrono::steady_clock::time_point> lastError_;

    // Error rate tracking
    mutable std::mutex errorRateMutex_;
    std::vector<std::chrono::steady_clock::time_point> recentErrors_;
    static constexpr size_t MAX_RECENT_ERRORS = 100;

    /**
     * @brief Record error occurrence for statistics
     * @param isConnectionError Whether error is connection-related
     * @param hasCacheFallback Whether cache fallback was used
     */
    void recordError(bool isConnectionError, bool hasCacheFallback);

    /**
     * @brief Update error rate calculation
     */
    void updateErrorRate();

    /**
     * @brief Calculate current error rate
     * @return Error rate (errors per minute)
     */
    double calculateErrorRate() const;

    /**
     * @brief Attempt to retry connection and read operation
     * @param nodeId Node identifier to read
     * @return ReadResult from retry attempt
     */
    ReadResult attemptRetry(const std::string& nodeId);

    /**
     * @brief Create error result with appropriate message
     * @param nodeId Node identifier
     * @param error Original error message
     * @param action Action taken (for message)
     * @return ReadResult with error information
     */
    ReadResult createErrorResult(const std::string& nodeId,
                                const std::string& error,
                                ErrorAction action);

    /**
     * @brief Get current timestamp in milliseconds
     * @return Current Unix timestamp in milliseconds
     */
    uint64_t getCurrentTimestamp();
};

} // namespace opcua2http
