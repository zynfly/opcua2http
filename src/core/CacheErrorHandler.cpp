#include "core/CacheErrorHandler.h"
#include "cache/CacheManager.h"
#include "opcua/OPCUAClient.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <thread>

namespace opcua2http {

CacheErrorHandler::CacheErrorHandler(CacheManager* cacheManager, OPCUAClient* opcClient)
    : cacheManager_(cacheManager)
    , opcClient_(opcClient)
    , lastError_(std::chrono::steady_clock::now()) {

    if (!cacheManager_) {
        throw std::invalid_argument("CacheManager cannot be null");
    }
    if (!opcClient_) {
        throw std::invalid_argument("OPCUAClient cannot be null");
    }

    spdlog::info("CacheErrorHandler initialized with max retry attempts: {}, retry delay: {}ms",
                 maxRetryAttempts_.load(), retryDelay_.count());
}

CacheErrorHandler::ErrorAction CacheErrorHandler::determineAction(
    const std::string& nodeId,
    const std::string& error,
    bool hasCachedData) {

    spdlog::debug("Determining error action for node {}: error='{}', hasCachedData={}",
                  nodeId, error, hasCachedData);

    // Check if it's a connection error
    bool isConnError = isConnectionError(error);

    // Record the error for statistics
    recordError(isConnError, hasCachedData);

    // If it's a connection error and we have cached data, return cached
    if (isConnError && hasCachedData) {
        spdlog::info("Connection error for node {}, returning cached data", nodeId);
        return ErrorAction::RETURN_CACHED;
    }

    // If it's a recoverable error and auto-retry is enabled, try retry
    if (isRecoverableError(error) && autoRetryEnabled_.load()) {
        spdlog::info("Recoverable error for node {}, attempting retry", nodeId);
        return ErrorAction::RETRY_CONNECTION;
    }

    // For timeout errors with cached data, prefer cached data
    if (isTimeoutError(error) && hasCachedData) {
        spdlog::info("Timeout error for node {}, returning cached data", nodeId);
        return ErrorAction::RETURN_CACHED;
    }

    // Default: return error to client
    spdlog::debug("Returning error to client for node {}", nodeId);
    return ErrorAction::RETURN_ERROR;
}

ReadResult CacheErrorHandler::handleConnectionError(
    const std::string& nodeId,
    const std::optional<CacheManager::CacheEntry>& cachedData) {

    spdlog::warn("Handling connection error for node: {}", nodeId);

    // Determine action based on cache availability
    ErrorAction action = determineAction(nodeId, "Connection error", cachedData.has_value());

    switch (action) {
        case ErrorAction::RETURN_CACHED:
            if (cachedData.has_value()) {
                cacheHitOnError_++;

                // Create result from cached data
                ReadResult result = cachedData->toReadResult();

                // Modify reason to indicate cache fallback
                auto cacheAge = cachedData->getAge();
                result.reason = "Connection Error - Using Cached Data (age: " +
                              std::to_string(cacheAge.count()) + "s)";

                spdlog::info("Returning cached data for node {} (age: {}s)",
                           nodeId, cacheAge.count());

                return result;
            }
            // Fall through to RETURN_ERROR if no cached data
            [[fallthrough]];

        case ErrorAction::RETURN_ERROR:
            cacheMissOnError_++;
            spdlog::error("No cached data available for node {} during connection error", nodeId);
            return createErrorResult(nodeId,
                "OPC UA server connection failed and no cached data available",
                ErrorAction::RETURN_ERROR);

        case ErrorAction::RETRY_CONNECTION:
            spdlog::info("Attempting retry for node {}", nodeId);
            return attemptRetry(nodeId);
    }

    // Should never reach here
    return createErrorResult(nodeId, "Unknown error handling path", ErrorAction::RETURN_ERROR);
}

std::vector<ReadResult> CacheErrorHandler::handlePartialBatchFailure(
    const std::vector<std::string>& nodeIds,
    const std::vector<ReadResult>& results) {

    if (nodeIds.size() != results.size()) {
        spdlog::error("Node IDs and results size mismatch in handlePartialBatchFailure");
        return results;
    }

    std::vector<ReadResult> enhancedResults;
    enhancedResults.reserve(results.size());

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        const auto& nodeId = nodeIds[i];

        // If result is successful, keep it as is
        if (result.success) {
            enhancedResults.push_back(result);
            continue;
        }

        // For failed results, try cache fallback
        spdlog::debug("Handling failure for node {} in batch", nodeId);

        auto cachedData = cacheManager_->getCachedValue(nodeId);

        if (cachedData.has_value()) {
            // Use cached data as fallback
            ReadResult fallbackResult = cachedData->toReadResult();
            auto cacheAge = cachedData->getAge();
            fallbackResult.reason = "Batch Read Failed - Using Cached Data (age: " +
                                  std::to_string(cacheAge.count()) + "s)";

            spdlog::info("Using cached fallback for failed node {} in batch (age: {}s)",
                       nodeId, cacheAge.count());

            cacheHitOnError_++;
            enhancedResults.push_back(fallbackResult);
        } else {
            // No cache available, keep original error
            spdlog::warn("No cached fallback available for failed node {} in batch", nodeId);
            cacheMissOnError_++;
            enhancedResults.push_back(result);
        }
    }

    return enhancedResults;
}

bool CacheErrorHandler::isConnectionError(const std::string& error) const {
    // Check for common connection error patterns
    std::string lowerError = error;
    std::transform(lowerError.begin(), lowerError.end(), lowerError.begin(), ::tolower);

    return lowerError.find("connection") != std::string::npos ||
           lowerError.find("connect") != std::string::npos ||
           lowerError.find("disconnected") != std::string::npos ||
           lowerError.find("network") != std::string::npos ||
           lowerError.find("unreachable") != std::string::npos ||
           lowerError.find("refused") != std::string::npos ||
           lowerError.find("closed") != std::string::npos;
}

bool CacheErrorHandler::isTimeoutError(const std::string& error) const {
    std::string lowerError = error;
    std::transform(lowerError.begin(), lowerError.end(), lowerError.begin(), ::tolower);

    return lowerError.find("timeout") != std::string::npos ||
           lowerError.find("timed out") != std::string::npos ||
           lowerError.find("time out") != std::string::npos;
}

bool CacheErrorHandler::isRecoverableError(const std::string& error) const {
    // Recoverable errors are typically connection or timeout errors
    return isConnectionError(error) || isTimeoutError(error);
}

CacheErrorHandler::ErrorStats CacheErrorHandler::getStats() const {
    return ErrorStats{
        totalErrors_.load(),
        connectionErrors_.load(),
        cacheHitOnError_.load(),
        cacheMissOnError_.load(),
        retryAttempts_.load(),
        successfulRetries_.load(),
        failedRetries_.load(),
        lastError_.load(),
        calculateErrorRate()
    };
}

void CacheErrorHandler::resetStats() {
    totalErrors_.store(0);
    connectionErrors_.store(0);
    cacheHitOnError_.store(0);
    cacheMissOnError_.store(0);
    retryAttempts_.store(0);
    successfulRetries_.store(0);
    failedRetries_.store(0);

    std::lock_guard<std::mutex> lock(errorRateMutex_);
    recentErrors_.clear();

    spdlog::info("Error statistics reset");
}

void CacheErrorHandler::setMaxRetryAttempts(int maxRetries) {
    maxRetryAttempts_.store(maxRetries);
    spdlog::info("Maximum retry attempts set to {}", maxRetries);
}

int CacheErrorHandler::getMaxRetryAttempts() const {
    return maxRetryAttempts_.load();
}

void CacheErrorHandler::setRetryDelay(std::chrono::milliseconds delay) {
    retryDelay_ = delay;
    spdlog::info("Retry delay set to {}ms", delay.count());
}

std::chrono::milliseconds CacheErrorHandler::getRetryDelay() const {
    return retryDelay_;
}

void CacheErrorHandler::setAutoRetryEnabled(bool enabled) {
    autoRetryEnabled_.store(enabled);
    spdlog::info("Automatic retry {}", enabled ? "enabled" : "disabled");
}

bool CacheErrorHandler::isAutoRetryEnabled() const {
    return autoRetryEnabled_.load();
}

void CacheErrorHandler::setErrorRateThreshold(double threshold) {
    errorRateThreshold_.store(threshold);
    spdlog::info("Error rate threshold set to {} errors/minute", threshold);
}

double CacheErrorHandler::getErrorRateThreshold() const {
    return errorRateThreshold_.load();
}

bool CacheErrorHandler::isErrorRateExceeded() const {
    double currentRate = calculateErrorRate();
    double threshold = errorRateThreshold_.load();
    return currentRate > threshold;
}

void CacheErrorHandler::recordError(bool isConnError, bool hasCacheFallback) {
    totalErrors_++;

    if (isConnError) {
        connectionErrors_++;
    }

    if (hasCacheFallback) {
        cacheHitOnError_++;
    } else {
        cacheMissOnError_++;
    }

    lastError_.store(std::chrono::steady_clock::now());

    // Update error rate tracking
    updateErrorRate();
}

void CacheErrorHandler::updateErrorRate() {
    std::lock_guard<std::mutex> lock(errorRateMutex_);

    auto now = std::chrono::steady_clock::now();
    recentErrors_.push_back(now);

    // Remove errors older than 1 minute
    auto oneMinuteAgo = now - std::chrono::minutes(1);
    recentErrors_.erase(
        std::remove_if(recentErrors_.begin(), recentErrors_.end(),
                      [oneMinuteAgo](const auto& timestamp) {
                          return timestamp < oneMinuteAgo;
                      }),
        recentErrors_.end()
    );

    // Keep only the most recent errors to prevent unbounded growth
    if (recentErrors_.size() > MAX_RECENT_ERRORS) {
        recentErrors_.erase(recentErrors_.begin(),
                          recentErrors_.begin() + (recentErrors_.size() - MAX_RECENT_ERRORS));
    }
}

double CacheErrorHandler::calculateErrorRate() const {
    std::lock_guard<std::mutex> lock(errorRateMutex_);

    if (recentErrors_.empty()) {
        return 0.0;
    }

    auto now = std::chrono::steady_clock::now();
    auto oneMinuteAgo = now - std::chrono::minutes(1);

    // Count errors in the last minute
    size_t errorsInLastMinute = std::count_if(
        recentErrors_.begin(), recentErrors_.end(),
        [oneMinuteAgo](const auto& timestamp) {
            return timestamp >= oneMinuteAgo;
        }
    );

    return static_cast<double>(errorsInLastMinute);
}

ReadResult CacheErrorHandler::attemptRetry(const std::string& nodeId) {
    int maxRetries = maxRetryAttempts_.load();

    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        retryAttempts_++;

        spdlog::info("Retry attempt {}/{} for node {}", attempt, maxRetries, nodeId);

        // Wait before retry (except for first attempt)
        if (attempt > 1) {
            std::this_thread::sleep_for(retryDelay_);
        }

        try {
            // Attempt to read from OPC UA server
            ReadResult result = opcClient_->readNode(nodeId);

            if (result.success) {
                successfulRetries_++;
                spdlog::info("Retry successful for node {} on attempt {}", nodeId, attempt);

                // Update cache with successful result
                cacheManager_->updateCache(nodeId, result.value,
                                         result.success ? "Good" : "Bad",
                                         result.reason, result.timestamp);

                return result;
            }

            spdlog::warn("Retry attempt {} failed for node {}: {}",
                       attempt, nodeId, result.reason);

        } catch (const std::exception& e) {
            spdlog::error("Exception during retry attempt {} for node {}: {}",
                        attempt, nodeId, e.what());
        }
    }

    // All retries failed
    failedRetries_++;
    spdlog::error("All {} retry attempts failed for node {}", maxRetries, nodeId);

    // Try cache fallback as last resort
    auto cachedData = cacheManager_->getCachedValue(nodeId);
    if (cachedData.has_value()) {
        ReadResult result = cachedData->toReadResult();
        auto cacheAge = cachedData->getAge();
        result.reason = "All retry attempts failed - Using Cached Data (age: " +
                      std::to_string(cacheAge.count()) + "s)";

        spdlog::info("Using cached data after failed retries for node {} (age: {}s)",
                   nodeId, cacheAge.count());

        return result;
    }

    // No cache available, return error
    return createErrorResult(nodeId,
        "Connection failed after " + std::to_string(maxRetries) + " retry attempts",
        ErrorAction::RETURN_ERROR);
}

ReadResult CacheErrorHandler::createErrorResult(
    const std::string& nodeId,
    const std::string& error,
    ErrorAction action) {

    std::string enhancedError = error;

    // Add action information to error message
    switch (action) {
        case ErrorAction::RETURN_CACHED:
            enhancedError += " (cache fallback used)";
            break;
        case ErrorAction::RETURN_ERROR:
            enhancedError += " (no cache available)";
            break;
        case ErrorAction::RETRY_CONNECTION:
            enhancedError += " (retry attempted)";
            break;
    }

    return ReadResult::createError(nodeId, enhancedError, getCurrentTimestamp());
}

uint64_t CacheErrorHandler::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

} // namespace opcua2http
