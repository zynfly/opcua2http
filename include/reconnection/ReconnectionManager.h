#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>

#include "config/Configuration.h"

namespace opcua2http {

// Forward declarations
class OPCUAClient;
class SubscriptionManager;

/**
 * @brief Manages automatic reconnection to OPC UA servers and subscription recovery
 * 
 * This class monitors the OPC UA connection status and automatically attempts
 * reconnection when the connection is lost. It also coordinates with the
 * SubscriptionManager to restore all active subscriptions after successful reconnection.
 * 
 * Key features:
 * - Background thread monitoring connection status
 * - Exponential backoff retry strategy
 * - Automatic subscription recovery after reconnection
 * - Configurable retry parameters via environment variables
 * - Detailed logging of reconnection activities
 */
class ReconnectionManager {
public:
    /**
     * @brief Reconnection state enumeration
     */
    enum class ReconnectionState {
        IDLE,                    // Not monitoring or reconnecting
        MONITORING,              // Monitoring connection status
        RECONNECTING,            // Actively attempting reconnection
        RECOVERING_SUBSCRIPTIONS // Restoring subscriptions after reconnection
    };
    
    /**
     * @brief Reconnection statistics for monitoring
     */
    struct ReconnectionStats {
        uint64_t totalReconnectionAttempts;      // Total reconnection attempts made
        uint64_t successfulReconnections;        // Successful reconnections
        uint64_t failedReconnections;            // Failed reconnections
        uint64_t subscriptionRecoveries;         // Subscription recovery attempts
        uint64_t successfulSubscriptionRecoveries; // Successful subscription recoveries
        std::chrono::steady_clock::time_point lastReconnectionAttempt; // Last attempt time
        std::chrono::steady_clock::time_point lastSuccessfulReconnection; // Last success time
        std::chrono::milliseconds totalDowntime; // Total accumulated downtime
        ReconnectionState currentState;          // Current state
        bool isMonitoring;                       // Whether monitoring is active
        int currentRetryAttempt;                 // Current retry attempt number
        std::chrono::milliseconds nextRetryDelay; // Next retry delay
    };
    
    /**
     * @brief Connection state change callback type
     * @param connected Whether the connection is now established
     * @param reconnected Whether this was a reconnection (vs initial connection)
     */
    using ConnectionStateCallback = std::function<void(bool connected, bool reconnected)>;

    /**
     * @brief Constructor
     * @param opcClient Pointer to the OPC UA client (must remain valid during lifetime)
     * @param subscriptionManager Pointer to the subscription manager (must remain valid during lifetime)
     * @param config Configuration containing reconnection parameters
     */
    ReconnectionManager(OPCUAClient* opcClient, SubscriptionManager* subscriptionManager, const Configuration& config);
    
    /**
     * @brief Destructor - stops monitoring and cleans up resources
     */
    ~ReconnectionManager();
    
    // Disable copy constructor and assignment operator
    ReconnectionManager(const ReconnectionManager&) = delete;
    ReconnectionManager& operator=(const ReconnectionManager&) = delete;
    
    /**
     * @brief Start monitoring the connection status
     * @return True if monitoring started successfully, false otherwise
     */
    bool startMonitoring();
    
    /**
     * @brief Stop monitoring the connection status
     */
    void stopMonitoring();
    
    /**
     * @brief Check if monitoring is currently active
     * @return True if monitoring is active, false otherwise
     */
    bool isMonitoring() const;
    
    /**
     * @brief Get current reconnection state
     * @return Current ReconnectionState
     */
    ReconnectionState getState() const;
    
    /**
     * @brief Get reconnection statistics
     * @return ReconnectionStats structure with current statistics
     */
    ReconnectionStats getStats() const;
    
    /**
     * @brief Manually trigger a reconnection attempt
     * @return True if reconnection was successful, false otherwise
     */
    bool triggerReconnection();
    
    /**
     * @brief Set connection state change callback
     * @param callback Callback function to be called on connection state changes
     */
    void setConnectionStateCallback(ConnectionStateCallback callback);
    
    /**
     * @brief Update configuration parameters
     * @param config New configuration
     */
    void updateConfiguration(const Configuration& config);
    
    /**
     * @brief Get detailed status information for monitoring
     * @return String containing detailed status information
     */
    std::string getDetailedStatus() const;
    
    /**
     * @brief Enable or disable detailed logging of reconnection activities
     * @param enabled Whether detailed logging should be enabled
     */
    void setDetailedLoggingEnabled(bool enabled);
    
    /**
     * @brief Check if detailed logging is enabled
     * @return True if detailed logging is enabled
     */
    bool isDetailedLoggingEnabled() const;
    
    /**
     * @brief Reset reconnection statistics
     */
    void resetStats();
    
    /**
     * @brief Check if currently in a reconnection attempt
     * @return True if actively reconnecting, false otherwise
     */
    bool isReconnecting() const;
    
    /**
     * @brief Get time until next reconnection attempt
     * @return Duration until next attempt, or zero if not scheduled
     */
    std::chrono::milliseconds getTimeUntilNextAttempt() const;

private:
    // Core components
    OPCUAClient* opcClient_;                              // OPC UA client reference
    SubscriptionManager* subscriptionManager_;           // Subscription manager reference
    
    // Configuration parameters (loaded from environment variables)
    int connectionRetryMax_;                             // Maximum retry attempts
    int connectionInitialDelay_;                         // Initial delay before first retry (ms)
    int connectionMaxRetry_;                             // Maximum number of retry cycles
    int connectionMaxDelay_;                             // Maximum delay between retries (ms)
    int connectionRetryDelay_;                           // Base retry delay (ms)
    
    // Thread management
    std::thread monitorThread_;                          // Background monitoring thread
    std::atomic<bool> monitoring_;                       // Whether monitoring is active
    std::atomic<ReconnectionState> currentState_;       // Current reconnection state
    mutable std::mutex stateMutex_;                      // Mutex for thread-safe state access
    
    // Reconnection state
    std::atomic<bool> reconnecting_;                     // Whether currently reconnecting
    std::atomic<int> currentRetryAttempt_;              // Current retry attempt number
    std::chrono::steady_clock::time_point lastAttemptTime_; // Last reconnection attempt time
    std::chrono::steady_clock::time_point disconnectionTime_; // When disconnection was detected
    std::chrono::steady_clock::time_point nextAttemptTime_; // When next attempt is scheduled
    
    // Statistics (atomic for thread-safe access)
    mutable std::atomic<uint64_t> totalReconnectionAttempts_{0};
    mutable std::atomic<uint64_t> successfulReconnections_{0};
    mutable std::atomic<uint64_t> failedReconnections_{0};
    mutable std::atomic<uint64_t> subscriptionRecoveries_{0};
    mutable std::atomic<uint64_t> successfulSubscriptionRecoveries_{0};
    mutable std::atomic<std::chrono::milliseconds> totalDowntime_{std::chrono::milliseconds::zero()};
    
    // Configuration
    std::atomic<bool> detailedLoggingEnabled_;          // Whether detailed logging is enabled
    ConnectionStateCallback connectionStateCallback_;    // Connection state change callback
    
    // Private methods
    
    /**
     * @brief Main monitoring loop (runs in background thread)
     */
    void monitoringLoop();
    
    /**
     * @brief Check current connection status
     * @return True if connected, false otherwise
     */
    bool checkConnectionStatus();
    
    /**
     * @brief Attempt to reconnect to the OPC UA server
     * @return True if reconnection successful, false otherwise
     */
    bool attemptReconnection();
    
    /**
     * @brief Recover all subscriptions after successful reconnection
     * @return True if subscription recovery successful, false otherwise
     */
    bool recoverSubscriptions();
    
    /**
     * @brief Calculate next retry delay using exponential backoff
     * @param attempt Current attempt number
     * @return Delay in milliseconds
     */
    std::chrono::milliseconds calculateRetryDelay(int attempt);
    
    /**
     * @brief Handle connection state change
     * @param connected Whether connection is established
     * @param wasReconnection Whether this was a reconnection
     */
    void handleConnectionStateChange(bool connected, bool wasReconnection);
    
    /**
     * @brief Update reconnection state
     * @param newState New state to set
     */
    void updateState(ReconnectionState newState);
    
    /**
     * @brief Log reconnection manager activity
     * @param message Log message
     * @param isError Whether this is an error message
     */
    void logActivity(const std::string& message, bool isError = false) const;
    
    /**
     * @brief Update downtime statistics
     */
    void updateDowntimeStats();
    
    /**
     * @brief Reset retry attempt counter
     */
    void resetRetryAttempts();
    
    /**
     * @brief Check if maximum retry attempts have been reached
     * @return True if max attempts reached, false otherwise
     */
    bool hasReachedMaxRetries() const;
    
    /**
     * @brief Wait for the specified duration or until monitoring is stopped
     * @param duration Duration to wait
     * @return True if wait completed normally, false if interrupted
     */
    bool waitOrStop(std::chrono::milliseconds duration);
    
    /**
     * @brief Validate configuration parameters
     * @param config Configuration to validate
     * @return True if configuration is valid, false otherwise
     */
    bool validateConfiguration(const Configuration& config) const;
};

} // namespace opcua2http