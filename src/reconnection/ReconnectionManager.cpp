#include "reconnection/ReconnectionManager.h"
#include "opcua/OPCUAClient.h"
#include "subscription/SubscriptionManager.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace opcua2http {

ReconnectionManager::ReconnectionManager(OPCUAClient* opcClient, SubscriptionManager* subscriptionManager, const Configuration& config)
    : opcClient_(opcClient)
    , subscriptionManager_(subscriptionManager)
    , monitoring_(false)
    , currentState_(ReconnectionState::IDLE)
    , reconnecting_(false)
    , currentRetryAttempt_(0)
    , detailedLoggingEnabled_(true)
    , connectionStateCallback_(nullptr)
{
    if (!opcClient_) {
        throw std::invalid_argument("OPCUAClient cannot be null");
    }
    if (!subscriptionManager_) {
        throw std::invalid_argument("SubscriptionManager cannot be null");
    }
    
    if (!validateConfiguration(config)) {
        throw std::invalid_argument("Invalid configuration provided");
    }
    
    updateConfiguration(config);
    
    logActivity("ReconnectionManager created");
}

ReconnectionManager::~ReconnectionManager() {
    logActivity("ReconnectionManager destructor called");
    stopMonitoring();
}

bool ReconnectionManager::startMonitoring() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (monitoring_.load()) {
        logActivity("Monitoring already active");
        return true;
    }
    
    logActivity("Starting connection monitoring");
    
    monitoring_.store(true);
    updateState(ReconnectionState::MONITORING);
    
    try {
        monitorThread_ = std::thread(&ReconnectionManager::monitoringLoop, this);
        logActivity("Connection monitoring thread started successfully");
        return true;
    } catch (const std::exception& e) {
        monitoring_.store(false);
        updateState(ReconnectionState::IDLE);
        std::ostringstream oss;
        oss << "Failed to start monitoring thread: " << e.what();
        logActivity(oss.str(), true);
        return false;
    }
}

void ReconnectionManager::stopMonitoring() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (!monitoring_.load()) {
        return;
    }
    
    logActivity("Stopping connection monitoring");
    
    monitoring_.store(false);
    updateState(ReconnectionState::IDLE);
    
    if (monitorThread_.joinable()) {
        monitorThread_.join();
        logActivity("Connection monitoring thread stopped");
    }
}

bool ReconnectionManager::isMonitoring() const {
    return monitoring_.load();
}

ReconnectionManager::ReconnectionState ReconnectionManager::getState() const {
    return currentState_.load();
}

ReconnectionManager::ReconnectionStats ReconnectionManager::getStats() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    ReconnectionStats stats;
    stats.totalReconnectionAttempts = totalReconnectionAttempts_.load();
    stats.successfulReconnections = successfulReconnections_.load();
    stats.failedReconnections = failedReconnections_.load();
    stats.subscriptionRecoveries = subscriptionRecoveries_.load();
    stats.successfulSubscriptionRecoveries = successfulSubscriptionRecoveries_.load();
    stats.lastReconnectionAttempt = lastAttemptTime_;
    stats.totalDowntime = totalDowntime_.load();
    stats.currentState = currentState_.load();
    stats.isMonitoring = monitoring_.load();
    stats.currentRetryAttempt = currentRetryAttempt_.load();
    stats.nextRetryDelay = const_cast<ReconnectionManager*>(this)->calculateRetryDelay(currentRetryAttempt_.load());
    
    return stats;
}

bool ReconnectionManager::triggerReconnection() {
    if (reconnecting_.load()) {
        logActivity("Reconnection already in progress");
        return false;
    }
    
    logActivity("Manual reconnection triggered");
    return attemptReconnection();
}

void ReconnectionManager::setConnectionStateCallback(ConnectionStateCallback callback) {
    connectionStateCallback_ = callback;
}

void ReconnectionManager::updateConfiguration(const Configuration& config) {
    if (!validateConfiguration(config)) {
        logActivity("Invalid configuration provided for update", true);
        return;
    }
    
    connectionRetryMax_ = config.connectionRetryMax;
    connectionInitialDelay_ = config.connectionInitialDelay;
    connectionMaxRetry_ = config.connectionMaxRetry;
    connectionMaxDelay_ = config.connectionMaxDelay;
    connectionRetryDelay_ = config.connectionRetryDelay;
    
    std::ostringstream oss;
    oss << "Configuration updated - RetryMax: " << connectionRetryMax_
        << ", InitialDelay: " << connectionInitialDelay_ << "ms"
        << ", MaxRetry: " << connectionMaxRetry_
        << ", MaxDelay: " << connectionMaxDelay_ << "ms"
        << ", RetryDelay: " << connectionRetryDelay_ << "ms";
    logActivity(oss.str());
}

std::string ReconnectionManager::getDetailedStatus() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    std::ostringstream oss;
    oss << "=== Reconnection Manager Status ===\n";
    
    // Current state
    oss << "Current State: ";
    switch (currentState_.load()) {
        case ReconnectionState::IDLE: oss << "IDLE"; break;
        case ReconnectionState::MONITORING: oss << "MONITORING"; break;
        case ReconnectionState::RECONNECTING: oss << "RECONNECTING"; break;
        case ReconnectionState::RECOVERING_SUBSCRIPTIONS: oss << "RECOVERING_SUBSCRIPTIONS"; break;
    }
    oss << "\n";
    
    oss << "Monitoring Active: " << (monitoring_.load() ? "Yes" : "No") << "\n";
    oss << "Currently Reconnecting: " << (reconnecting_.load() ? "Yes" : "No") << "\n";
    oss << "Current Retry Attempt: " << currentRetryAttempt_.load() << "\n";
    
    // Configuration
    oss << "\n=== Configuration ===\n";
    oss << "Connection Retry Max: " << connectionRetryMax_ << "\n";
    oss << "Connection Initial Delay: " << connectionInitialDelay_ << "ms\n";
    oss << "Connection Max Retry: " << connectionMaxRetry_ << "\n";
    oss << "Connection Max Delay: " << connectionMaxDelay_ << "ms\n";
    oss << "Connection Retry Delay: " << connectionRetryDelay_ << "ms\n";
    oss << "Detailed Logging Enabled: " << (detailedLoggingEnabled_.load() ? "Yes" : "No") << "\n";
    
    // Statistics
    oss << "\n=== Statistics ===\n";
    oss << "Total Reconnection Attempts: " << totalReconnectionAttempts_.load() << "\n";
    oss << "Successful Reconnections: " << successfulReconnections_.load() << "\n";
    oss << "Failed Reconnections: " << failedReconnections_.load() << "\n";
    oss << "Subscription Recoveries: " << subscriptionRecoveries_.load() << "\n";
    oss << "Successful Subscription Recoveries: " << successfulSubscriptionRecoveries_.load() << "\n";
    
    auto totalDowntimeMs = totalDowntime_.load();
    oss << "Total Downtime: " << totalDowntimeMs.count() << "ms";
    if (totalDowntimeMs.count() > 0) {
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(totalDowntimeMs);
        if (seconds.count() > 0) {
            oss << " (" << seconds.count() << "s)";
        }
    }
    oss << "\n";
    
    // Timing information
    auto now = std::chrono::steady_clock::now();
    if (lastAttemptTime_ != std::chrono::steady_clock::time_point{}) {
        auto timeSinceLastAttempt = std::chrono::duration_cast<std::chrono::seconds>(now - lastAttemptTime_);
        oss << "Time Since Last Attempt: " << timeSinceLastAttempt.count() << "s\n";
    }
    
    if (nextAttemptTime_ != std::chrono::steady_clock::time_point{} && nextAttemptTime_ > now) {
        auto timeUntilNext = std::chrono::duration_cast<std::chrono::seconds>(nextAttemptTime_ - now);
        oss << "Time Until Next Attempt: " << timeUntilNext.count() << "s\n";
    }
    
    // Connection status
    oss << "\n=== Connection Status ===\n";
    oss << "OPC UA Client Connected: " << (opcClient_->isConnected() ? "Yes" : "No") << "\n";
    oss << "OPC UA Client State: ";
    switch (opcClient_->getConnectionState()) {
        case OPCUAClient::ConnectionState::DISCONNECTED: oss << "DISCONNECTED"; break;
        case OPCUAClient::ConnectionState::CONNECTING: oss << "CONNECTING"; break;
        case OPCUAClient::ConnectionState::CONNECTED: oss << "CONNECTED"; break;
        case OPCUAClient::ConnectionState::RECONNECTING: oss << "RECONNECTING"; break;
        case OPCUAClient::ConnectionState::CONNECTION_ERROR: oss << "CONNECTION_ERROR"; break;
    }
    oss << "\n";
    
    return oss.str();
}

void ReconnectionManager::setDetailedLoggingEnabled(bool enabled) {
    detailedLoggingEnabled_.store(enabled);
    std::ostringstream oss;
    oss << "Detailed logging " << (enabled ? "enabled" : "disabled");
    logActivity(oss.str());
}

bool ReconnectionManager::isDetailedLoggingEnabled() const {
    return detailedLoggingEnabled_.load();
}

void ReconnectionManager::resetStats() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    totalReconnectionAttempts_.store(0);
    successfulReconnections_.store(0);
    failedReconnections_.store(0);
    subscriptionRecoveries_.store(0);
    successfulSubscriptionRecoveries_.store(0);
    totalDowntime_.store(std::chrono::milliseconds::zero());
    
    lastAttemptTime_ = std::chrono::steady_clock::time_point{};
    disconnectionTime_ = std::chrono::steady_clock::time_point{};
    nextAttemptTime_ = std::chrono::steady_clock::time_point{};
    
    logActivity("Reconnection statistics reset");
}

bool ReconnectionManager::isReconnecting() const {
    return reconnecting_.load();
}

std::chrono::milliseconds ReconnectionManager::getTimeUntilNextAttempt() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    
    if (nextAttemptTime_ == std::chrono::steady_clock::time_point{}) {
        return std::chrono::milliseconds::zero();
    }
    
    auto now = std::chrono::steady_clock::now();
    if (nextAttemptTime_ <= now) {
        return std::chrono::milliseconds::zero();
    }
    
    return std::chrono::duration_cast<std::chrono::milliseconds>(nextAttemptTime_ - now);
}

// Private methods

void ReconnectionManager::monitoringLoop() {
    logActivity("Connection monitoring loop started");
    
    bool wasConnected = opcClient_->isConnected();
    bool connectionLost = false;
    
    while (monitoring_.load()) {
        try {
            bool isConnected = checkConnectionStatus();
            
            // Detect connection state changes
            if (wasConnected && !isConnected) {
                // Connection lost
                logActivity("Connection lost detected", true);
                connectionLost = true;
                disconnectionTime_ = std::chrono::steady_clock::now();
                resetRetryAttempts();
                handleConnectionStateChange(false, false);
            } else if (!wasConnected && isConnected && connectionLost) {
                // Connection restored (but not through our reconnection)
                logActivity("Connection restored externally");
                connectionLost = false;
                handleConnectionStateChange(true, false);
            }
            

            
            // If connection is lost, attempt reconnection
            if (connectionLost && !isConnected) {
                updateState(ReconnectionState::RECONNECTING);
                
                if (attemptReconnection()) {
                    // Reconnection successful
                    connectionLost = false;
                    handleConnectionStateChange(true, true);
                    updateState(ReconnectionState::MONITORING);
                } else {
                    // Reconnection failed, wait before next attempt
                    if (!hasReachedMaxRetries()) {
                        auto delay = calculateRetryDelay(currentRetryAttempt_.load());
                        nextAttemptTime_ = std::chrono::steady_clock::now() + delay;
                        
                        if (detailedLoggingEnabled_.load()) {
                            std::ostringstream oss;
                            oss << "Waiting " << delay.count() << "ms before next reconnection attempt";
                            logActivity(oss.str());
                        }
                        
                        if (!waitOrStop(delay)) {
                            // Monitoring was stopped during wait
                            break;
                        }
                    } else {
                        std::ostringstream oss;
                        oss << "Maximum retry attempts (" << connectionMaxRetry_ << ") reached, stopping reconnection attempts";
                        logActivity(oss.str(), true);
                        
                        // Wait longer before resetting retry counter
                        auto longDelay = std::chrono::milliseconds(connectionMaxDelay_ * 2);
                        if (waitOrStop(longDelay)) {
                            resetRetryAttempts();
                            logActivity("Retry counter reset, resuming reconnection attempts");
                        }
                    }
                }
            } else {
                updateState(ReconnectionState::MONITORING);
            }
            
            wasConnected = isConnected;
            
            // Regular monitoring interval (shorter for faster detection in tests)
            auto monitorInterval = isConnected ? 
                std::chrono::milliseconds(1000) :  // 1 second when connected
                std::chrono::milliseconds(500);    // 500ms when disconnected
            
            if (!waitOrStop(monitorInterval)) {
                break;
            }
            
        } catch (const std::exception& e) {
            std::ostringstream oss;
            oss << "Exception in monitoring loop: " << e.what();
            logActivity(oss.str(), true);
            
            // Wait a bit before continuing to avoid tight error loops
            if (!waitOrStop(std::chrono::milliseconds(1000))) {
                break;
            }
        }
    }
    
    updateState(ReconnectionState::IDLE);
    logActivity("Connection monitoring loop stopped");
}

bool ReconnectionManager::checkConnectionStatus() {
    if (!opcClient_) {
        return false;
    }
    
    // Use the OPC UA client's connection status
    bool connected = opcClient_->isConnected();
    
    if (detailedLoggingEnabled_.load()) {
        // Periodically log connection status (every 30 seconds when connected)
        static auto lastStatusLog = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastLog = std::chrono::duration_cast<std::chrono::seconds>(now - lastStatusLog);
        
        if (connected && timeSinceLastLog.count() >= 30) {
            logActivity("Connection status: CONNECTED");
            lastStatusLog = now;
        } else if (!connected) {
            // Log disconnection status more frequently
            static auto lastDisconnectLog = std::chrono::steady_clock::now();
            auto timeSinceDisconnectLog = std::chrono::duration_cast<std::chrono::seconds>(now - lastDisconnectLog);
            if (timeSinceDisconnectLog.count() >= 1) { // Log every second when disconnected
                logActivity("Connection status: DISCONNECTED", true);
                lastDisconnectLog = now;
            }
        }
    }
    
    return connected;
}

bool ReconnectionManager::attemptReconnection() {
    if (reconnecting_.load()) {
        return false;
    }
    
    reconnecting_.store(true);
    currentRetryAttempt_.fetch_add(1);
    totalReconnectionAttempts_.fetch_add(1);
    lastAttemptTime_ = std::chrono::steady_clock::now();
    
    std::ostringstream oss;
    oss << "Attempting reconnection (attempt " << currentRetryAttempt_.load() 
        << " of " << connectionMaxRetry_ << ")";
    logActivity(oss.str());
    
    bool success = false;
    bool wasConnected = opcClient_->isConnected();
    
    try {
        // Attempt to reconnect
        success = opcClient_->connect();
        
        if (success) {
            successfulReconnections_.fetch_add(1);
            logActivity("Reconnection successful");
            
            // Update downtime statistics
            updateDowntimeStats();
            
            // Trigger connection state callback for successful reconnection
            if (!wasConnected) {
                handleConnectionStateChange(true, true);
            }
            
            // Attempt to recover subscriptions
            if (recoverSubscriptions()) {
                logActivity("Subscription recovery completed successfully");
            } else {
                logActivity("Subscription recovery failed", true);
                // Don't fail the entire reconnection for subscription recovery failure
            }
        } else {
            failedReconnections_.fetch_add(1);
            std::ostringstream failOss;
            failOss << "Reconnection attempt " << currentRetryAttempt_.load() << " failed";
            logActivity(failOss.str(), true);
        }
        
    } catch (const std::exception& e) {
        failedReconnections_.fetch_add(1);
        std::ostringstream exOss;
        exOss << "Exception during reconnection attempt: " << e.what();
        logActivity(exOss.str(), true);
    }
    
    reconnecting_.store(false);
    return success;
}

bool ReconnectionManager::recoverSubscriptions() {
    if (!subscriptionManager_) {
        logActivity("No subscription manager available for recovery", true);
        return false;
    }
    
    updateState(ReconnectionState::RECOVERING_SUBSCRIPTIONS);
    subscriptionRecoveries_.fetch_add(1);
    
    logActivity("Starting subscription recovery");
    
    try {
        bool success = subscriptionManager_->recreateAllMonitoredItems();
        
        if (success) {
            successfulSubscriptionRecoveries_.fetch_add(1);
            
            auto activeItems = subscriptionManager_->getActiveMonitoredItems();
            std::ostringstream oss;
            oss << "Successfully recovered " << activeItems.size() << " subscriptions";
            logActivity(oss.str());
            
            if (detailedLoggingEnabled_.load() && !activeItems.empty()) {
                std::ostringstream detailOss;
                detailOss << "Recovered subscriptions for nodes: ";
                for (size_t i = 0; i < activeItems.size() && i < 5; ++i) {  // Log first 5 items
                    if (i > 0) detailOss << ", ";
                    detailOss << activeItems[i];
                }
                if (activeItems.size() > 5) {
                    detailOss << " and " << (activeItems.size() - 5) << " more";
                }
                logActivity(detailOss.str());
            }
        } else {
            logActivity("Subscription recovery failed", true);
        }
        
        return success;
        
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Exception during subscription recovery: " << e.what();
        logActivity(oss.str(), true);
        return false;
    }
}

std::chrono::milliseconds ReconnectionManager::calculateRetryDelay(int attempt) {
    if (attempt <= 0) {
        return std::chrono::milliseconds(connectionInitialDelay_);
    }
    
    // Exponential backoff with jitter
    double baseDelay = static_cast<double>(connectionRetryDelay_);
    int exponent = (attempt - 1 < 10) ? (attempt - 1) : 10; // Cap at 2^10
    double exponentialDelay = baseDelay * pow(2.0, static_cast<double>(exponent));
    
    // Add some jitter (±10%)
    double jitter = 0.9 + (static_cast<double>(rand()) / RAND_MAX) * 0.2; // 0.9 to 1.1
    exponentialDelay *= jitter;
    
    // Cap at maximum delay
    double maxDelay = static_cast<double>(connectionMaxDelay_);
    double finalDelay = (exponentialDelay < maxDelay) ? exponentialDelay : maxDelay;
    
    return std::chrono::milliseconds(static_cast<long long>(finalDelay));
}

void ReconnectionManager::handleConnectionStateChange(bool connected, bool wasReconnection) {
    if (connectionStateCallback_) {
        try {
            connectionStateCallback_(connected, wasReconnection);
        } catch (const std::exception& e) {
            std::ostringstream oss;
            oss << "Exception in connection state callback: " << e.what();
            logActivity(oss.str(), true);
        }
    }
    
    std::ostringstream oss;
    oss << "Connection state changed: " << (connected ? "CONNECTED" : "DISCONNECTED");
    if (wasReconnection) {
        oss << " (reconnection)";
    }
    logActivity(oss.str(), !connected);
}

void ReconnectionManager::updateState(ReconnectionState newState) {
    ReconnectionState oldState = currentState_.exchange(newState);
    
    if (oldState != newState && detailedLoggingEnabled_.load()) {
        std::ostringstream oss;
        oss << "State changed: ";
        
        switch (oldState) {
            case ReconnectionState::IDLE: oss << "IDLE"; break;
            case ReconnectionState::MONITORING: oss << "MONITORING"; break;
            case ReconnectionState::RECONNECTING: oss << "RECONNECTING"; break;
            case ReconnectionState::RECOVERING_SUBSCRIPTIONS: oss << "RECOVERING_SUBSCRIPTIONS"; break;
        }
        
        oss << " -> ";
        
        switch (newState) {
            case ReconnectionState::IDLE: oss << "IDLE"; break;
            case ReconnectionState::MONITORING: oss << "MONITORING"; break;
            case ReconnectionState::RECONNECTING: oss << "RECONNECTING"; break;
            case ReconnectionState::RECOVERING_SUBSCRIPTIONS: oss << "RECOVERING_SUBSCRIPTIONS"; break;
        }
        
        logActivity(oss.str());
    }
}

void ReconnectionManager::logActivity(const std::string& message, bool isError) const {
    std::string prefix = isError ? "[ERROR] " : "[INFO] ";
    std::cout << prefix << "ReconnectionManager: " << message << std::endl;
}

void ReconnectionManager::updateDowntimeStats() {
    if (disconnectionTime_ != std::chrono::steady_clock::time_point{}) {
        auto now = std::chrono::steady_clock::now();
        auto downtime = std::chrono::duration_cast<std::chrono::milliseconds>(now - disconnectionTime_);
        auto currentDowntime = totalDowntime_.load();
        totalDowntime_.store(currentDowntime + downtime);
        
        if (detailedLoggingEnabled_.load()) {
            std::ostringstream oss;
            oss << "Downtime for this disconnection: " << downtime.count() << "ms";
            logActivity(oss.str());
        }
        
        disconnectionTime_ = std::chrono::steady_clock::time_point{};
    }
}

void ReconnectionManager::resetRetryAttempts() {
    currentRetryAttempt_.store(0);
    nextAttemptTime_ = std::chrono::steady_clock::time_point{};
}

bool ReconnectionManager::hasReachedMaxRetries() const {
    return currentRetryAttempt_.load() >= connectionMaxRetry_;
}

bool ReconnectionManager::waitOrStop(std::chrono::milliseconds duration) {
    auto endTime = std::chrono::steady_clock::now() + duration;
    
    while (monitoring_.load() && std::chrono::steady_clock::now() < endTime) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Check every 100ms
    }
    
    return monitoring_.load(); // Return true if we completed the wait, false if stopped
}

bool ReconnectionManager::validateConfiguration(const Configuration& config) const {
    if (config.connectionRetryMax < 0) {
        logActivity("Invalid connectionRetryMax: must be non-negative", true);
        return false;
    }
    
    if (config.connectionInitialDelay < 0) {
        logActivity("Invalid connectionInitialDelay: must be non-negative", true);
        return false;
    }
    
    if (config.connectionMaxRetry <= 0) {
        logActivity("Invalid connectionMaxRetry: must be positive", true);
        return false;
    }
    
    if (config.connectionMaxDelay <= 0) {
        logActivity("Invalid connectionMaxDelay: must be positive", true);
        return false;
    }
    
    if (config.connectionRetryDelay <= 0) {
        logActivity("Invalid connectionRetryDelay: must be positive", true);
        return false;
    }
    
    return true;
}

} // namespace opcua2http