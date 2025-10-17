#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include <atomic>
#include <functional>
#include <memory>

#include <open62541/client.h>
#include <open62541/client_subscriptions.h>

#include "cache/CacheManager.h"
#include "core/ReadResult.h"

namespace opcua2http {

// Forward declaration
class OPCUAClient;

/**
 * @brief Manages OPC UA subscriptions and monitored items for on-demand data monitoring
 * 
 * This class implements the core subscription management functionality:
 * - Creates subscriptions on-demand when data points are first requested
 * - Manages monitored items lifecycle (create/delete)
 * - Handles data change notifications and updates cache
 * - Automatically cleans up unused monitored items
 * - Provides subscription recovery after reconnection
 */
class SubscriptionManager {
public:
    /**
     * @brief Information about a monitored item
     */
    struct MonitoredItemInfo {
        std::string nodeId;                                    // OPC UA node identifier
        UA_UInt32 monitoredItemId;                            // Server-assigned monitored item ID
        UA_UInt32 clientHandle;                               // Client-assigned handle
        std::chrono::steady_clock::time_point lastAccessed;  // Last access time for cleanup
        bool isActive;                                        // Whether the monitored item is active
        
        MonitoredItemInfo() : monitoredItemId(0), clientHandle(0), isActive(false) {
            lastAccessed = std::chrono::steady_clock::now();
        }
        
        MonitoredItemInfo(const std::string& id, UA_UInt32 monId, UA_UInt32 handle)
            : nodeId(id), monitoredItemId(monId), clientHandle(handle), isActive(true) {
            lastAccessed = std::chrono::steady_clock::now();
        }
    };
    
    /**
     * @brief Subscription statistics for monitoring
     */
    struct SubscriptionStats {
        UA_UInt32 subscriptionId;                             // Main subscription ID
        size_t totalMonitoredItems;                           // Total monitored items
        size_t activeMonitoredItems;                          // Active monitored items
        size_t inactiveMonitoredItems;                        // Inactive monitored items
        uint64_t totalNotifications;                          // Total data change notifications received
        uint64_t totalErrors;                                 // Total errors encountered
        std::chrono::steady_clock::time_point creationTime;  // Subscription creation time
        std::chrono::steady_clock::time_point lastActivity;  // Last activity time
        bool isSubscriptionActive;                            // Whether main subscription is active
    };

    /**
     * @brief Constructor
     * @param opcClient Pointer to the OPC UA client (must remain valid during lifetime)
     * @param cacheManager Pointer to the cache manager (must remain valid during lifetime)
     * @param itemExpireMinutes Minutes after which unused monitored items are cleaned up (default: 30)
     */
    SubscriptionManager(OPCUAClient* opcClient, CacheManager* cacheManager, int itemExpireMinutes = 30);
    
    /**
     * @brief Destructor - cleans up all subscriptions and monitored items
     */
    ~SubscriptionManager();
    
    // Disable copy constructor and assignment operator
    SubscriptionManager(const SubscriptionManager&) = delete;
    SubscriptionManager& operator=(const SubscriptionManager&) = delete;
    
    /**
     * @brief Initialize the subscription manager and create the main subscription
     * @return True if initialization successful, false otherwise
     */
    bool initializeSubscription();
    
    /**
     * @brief Add a monitored item for the specified node ID
     * @param nodeId OPC UA node identifier
     * @return True if monitored item was created successfully, false otherwise
     */
    bool addMonitoredItem(const std::string& nodeId);
    
    /**
     * @brief Remove a monitored item for the specified node ID
     * @param nodeId OPC UA node identifier
     * @return True if monitored item was removed successfully, false otherwise
     */
    bool removeMonitoredItem(const std::string& nodeId);
    
    /**
     * @brief Get all active monitored item node IDs
     * @return Vector of node identifiers that have active monitored items
     */
    std::vector<std::string> getActiveMonitoredItems() const;
    
    /**
     * @brief Get all monitored item node IDs (active and inactive)
     * @return Vector of all monitored item node identifiers
     */
    std::vector<std::string> getAllMonitoredItems() const;
    
    /**
     * @brief Recreate all monitored items (used after reconnection)
     * @return True if all monitored items were recreated successfully, false otherwise
     */
    bool recreateAllMonitoredItems();
    
    /**
     * @brief Clean up unused monitored items that haven't been accessed recently
     * @return Number of monitored items removed
     */
    size_t cleanupUnusedItems();
    
    /**
     * @brief Update last accessed time for a monitored item (called when data is requested)
     * @param nodeId OPC UA node identifier
     */
    void updateLastAccessed(const std::string& nodeId);
    
    /**
     * @brief Check if a monitored item exists for the specified node ID
     * @param nodeId OPC UA node identifier
     * @return True if monitored item exists, false otherwise
     */
    bool hasMonitoredItem(const std::string& nodeId) const;
    
    /**
     * @brief Get subscription statistics
     * @return SubscriptionStats structure with current statistics
     */
    SubscriptionStats getStats() const;
    
    /**
     * @brief Clear all monitored items and reset subscription
     * @return True if cleanup successful, false otherwise
     */
    bool clearAllMonitoredItems();
    
    /**
     * @brief Check if the main subscription is active
     * @return True if subscription is active, false otherwise
     */
    bool isSubscriptionActive() const;
    
    /**
     * @brief Get the main subscription ID
     * @return Subscription ID, or 0 if no subscription exists
     */
    UA_UInt32 getSubscriptionId() const;
    
    /**
     * @brief Set the item expiration time
     * @param minutes Minutes after which unused items are cleaned up
     */
    void setItemExpireTime(int minutes);
    
    /**
     * @brief Get the current item expiration time
     * @return Item expiration time in minutes
     */
    int getItemExpireTime() const;
    
    /**
     * @brief Enable or disable automatic cleanup of unused monitored items
     * @param enabled Whether automatic cleanup should be enabled
     */
    void setAutoCleanupEnabled(bool enabled);
    
    /**
     * @brief Check if automatic cleanup is enabled
     * @return True if automatic cleanup is enabled
     */
    bool isAutoCleanupEnabled() const;
    
    /**
     * @brief Get monitored items that haven't been accessed recently
     * @return Vector of node IDs for items that could be cleaned up
     */
    std::vector<std::string> getUnusedMonitoredItems() const;
    
    /**
     * @brief Get detailed status information for monitoring
     * @return String containing detailed status information
     */
    std::string getDetailedStatus() const;
    
    /**
     * @brief Enable or disable detailed logging of subscription activities
     * @param enabled Whether detailed logging should be enabled
     */
    void setDetailedLoggingEnabled(bool enabled);
    
    /**
     * @brief Check if detailed logging is enabled
     * @return True if detailed logging is enabled
     */
    bool isDetailedLoggingEnabled() const;

    // Static callback functions for open62541 (must be static for C API compatibility)
    
    /**
     * @brief Data change notification callback (called by open62541)
     * @param client OPC UA client instance
     * @param subId Subscription ID
     * @param subContext Subscription context (pointer to SubscriptionManager)
     * @param monId Monitored item ID
     * @param monContext Monitored item context (pointer to node ID string)
     * @param value New data value
     */
    static void dataChangeNotificationCallback(UA_Client *client, UA_UInt32 subId, 
                                             void *subContext, UA_UInt32 monId, 
                                             void *monContext, UA_DataValue *value);
    
    /**
     * @brief Subscription inactivity callback (called by open62541)
     * @param client OPC UA client instance
     * @param subId Subscription ID
     * @param subContext Subscription context (pointer to SubscriptionManager)
     */
    static void subscriptionInactivityCallback(UA_Client *client, UA_UInt32 subId, 
                                             void *subContext);
    
    /**
     * @brief Subscription status change callback (called by open62541)
     * @param client OPC UA client instance
     * @param subId Subscription ID
     * @param subContext Subscription context (pointer to SubscriptionManager)
     * @param notification Status change notification
     */
    static void subscriptionStatusChangeCallback(UA_Client *client, UA_UInt32 subId, 
                                               void *subContext, UA_StatusChangeNotification *notification);

private:
    // Core components
    OPCUAClient* opcClient_;                                  // OPC UA client reference
    CacheManager* cacheManager_;                              // Cache manager reference
    
    // Subscription management
    mutable std::mutex subscriptionMutex_;                   // Mutex for thread safety
    UA_UInt32 subscriptionId_;                               // Main subscription ID
    std::atomic<bool> subscriptionActive_;                   // Whether subscription is active
    
    // Monitored items management
    std::unordered_map<std::string, MonitoredItemInfo> monitoredItems_; // Node ID -> MonitoredItemInfo
    std::unordered_map<UA_UInt32, std::string> handleToNodeId_;         // Client handle -> Node ID
    std::atomic<UA_UInt32> nextClientHandle_;                           // Next client handle to assign
    
    // Configuration
    std::chrono::minutes itemExpireTime_;                    // Item expiration time
    std::atomic<bool> autoCleanupEnabled_;                   // Whether automatic cleanup is enabled
    std::atomic<bool> detailedLoggingEnabled_;               // Whether detailed logging is enabled
    
    // Statistics (atomic for thread-safe access)
    mutable std::atomic<uint64_t> totalNotifications_{0};   // Total notifications received
    mutable std::atomic<uint64_t> totalErrors_{0};          // Total errors encountered
    std::chrono::steady_clock::time_point creationTime_;    // Manager creation time
    mutable std::atomic<std::chrono::steady_clock::time_point> lastActivity_; // Last activity time
    
    // Private methods
    
    /**
     * @brief Create the main OPC UA subscription
     * @return Subscription response, check statusCode for success
     */
    UA_CreateSubscriptionResponse createOPCSubscription();
    
    /**
     * @brief Create a monitored item for the specified node ID
     * @param nodeId OPC UA node identifier
     * @return Monitored item creation result, check statusCode for success
     */
    UA_MonitoredItemCreateResult createMonitoredItem(const std::string& nodeId);
    
    /**
     * @brief Delete a monitored item by its ID
     * @param monitoredItemId Monitored item ID to delete
     * @return True if deletion successful, false otherwise
     */
    bool deleteMonitoredItem(UA_UInt32 monitoredItemId);
    
    /**
     * @brief Handle data change notification (called from static callback)
     * @param monId Monitored item ID
     * @param value New data value
     */
    void handleDataChangeNotification(UA_UInt32 monId, const UA_DataValue* value);
    
    /**
     * @brief Handle subscription inactivity (called from static callback)
     */
    void handleSubscriptionInactivity();
    
    /**
     * @brief Handle subscription status change (called from static callback)
     * @param notification Status change notification
     */
    void handleSubscriptionStatusChange(UA_StatusChangeNotification *notification);
    
    /**
     * @brief Convert UA_DataValue to ReadResult
     * @param nodeId Node identifier
     * @param value OPC UA data value
     * @return ReadResult structure
     */
    ReadResult convertDataValueToReadResult(const std::string& nodeId, const UA_DataValue* value);
    
    /**
     * @brief Get next available client handle
     * @return Unique client handle
     */
    UA_UInt32 getNextClientHandle();
    
    /**
     * @brief Check if a monitored item is expired (not accessed recently)
     * @param info Monitored item info to check
     * @return True if item is expired, false otherwise
     */
    bool isMonitoredItemExpired(const MonitoredItemInfo& info) const;
    
    /**
     * @brief Log subscription manager activity
     * @param message Log message
     * @param isError Whether this is an error message
     */
    void logActivity(const std::string& message, bool isError = false) const;
    
    /**
     * @brief Update activity timestamp
     */
    void updateActivity() const;
    
    /**
     * @brief Validate node ID format
     * @param nodeId Node ID to validate
     * @return True if format is valid, false otherwise
     */
    bool validateNodeId(const std::string& nodeId) const;
    
    /**
     * @brief Update last accessed time without locking (assumes mutex is held)
     * @param nodeId Node ID to update
     */
    void updateLastAccessedUnsafe(const std::string& nodeId);
};

} // namespace opcua2http