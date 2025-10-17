#include "subscription/SubscriptionManager.h"
#include "opcua/OPCUAClient.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace opcua2http {

SubscriptionManager::SubscriptionManager(OPCUAClient* opcClient, CacheManager* cacheManager, int itemExpireMinutes)
    : opcClient_(opcClient)
    , cacheManager_(cacheManager)
    , subscriptionId_(0)
    , subscriptionActive_(false)
    , nextClientHandle_(1000)  // Start from 1000 to avoid conflicts
    , itemExpireTime_(itemExpireMinutes)
    , autoCleanupEnabled_(true)
    , detailedLoggingEnabled_(true)
    , creationTime_(std::chrono::steady_clock::now())
{
    if (!opcClient_) {
        throw std::invalid_argument("OPCUAClient cannot be null");
    }
    if (!cacheManager_) {
        throw std::invalid_argument("CacheManager cannot be null");
    }
    
    lastActivity_.store(creationTime_);
    logActivity("SubscriptionManager created");
}

SubscriptionManager::~SubscriptionManager() {
    logActivity("SubscriptionManager destructor called");
    clearAllMonitoredItems();
}

bool SubscriptionManager::initializeSubscription() {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    if (!opcClient_->isConnected()) {
        logActivity("Cannot initialize subscription: OPC UA client not connected", true);
        return false;
    }
    
    if (subscriptionActive_.load()) {
        logActivity("Subscription already initialized");
        return true;
    }
    
    logActivity("Initializing OPC UA subscription");
    
    UA_CreateSubscriptionResponse response = createOPCSubscription();
    if (response.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        std::ostringstream oss;
        oss << "Failed to create subscription: " << UA_StatusCode_name(response.responseHeader.serviceResult);
        logActivity(oss.str(), true);
        return false;
    }
    
    subscriptionId_ = response.subscriptionId;
    subscriptionActive_.store(true);
    
    std::ostringstream oss;
    oss << "Subscription created successfully with ID: " << subscriptionId_;
    logActivity(oss.str());
    
    updateActivity();
    return true;
}

bool SubscriptionManager::addMonitoredItem(const std::string& nodeId) {
    if (nodeId.empty() || !validateNodeId(nodeId)) {
        logActivity("Invalid node ID: " + nodeId, true);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    // Check if monitored item already exists
    auto it = monitoredItems_.find(nodeId);
    if (it != monitoredItems_.end()) {
        if (it->second.isActive) {
            logActivity("Monitored item already exists for node: " + nodeId);
            updateLastAccessedUnsafe(nodeId);
            return true;
        } else {
            // Remove inactive item first
            monitoredItems_.erase(it);
        }
    }
    
    if (!subscriptionActive_.load()) {
        // Create subscription directly without calling initializeSubscription to avoid deadlock
        logActivity("Initializing OPC UA subscription");
        
        UA_CreateSubscriptionResponse response = createOPCSubscription();
        if (response.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
            std::ostringstream oss;
            oss << "Failed to create subscription: " << UA_StatusCode_name(response.responseHeader.serviceResult);
            logActivity(oss.str(), true);
            return false;
        }
        
        subscriptionId_ = response.subscriptionId;
        subscriptionActive_.store(true);
        
        std::ostringstream oss;
        oss << "Subscription created successfully with ID: " << subscriptionId_;
        logActivity(oss.str());
        
        updateActivity();
    }
    
    if (!opcClient_->isConnected()) {
        logActivity("Cannot add monitored item: OPC UA client not connected", true);
        return false;
    }
    
    logActivity("Creating monitored item for node: " + nodeId);
    
    UA_MonitoredItemCreateResult result = createMonitoredItem(nodeId);
    if (result.statusCode != UA_STATUSCODE_GOOD) {
        std::ostringstream oss;
        oss << "Failed to create monitored item for node " << nodeId 
            << ": " << UA_StatusCode_name(result.statusCode);
        logActivity(oss.str(), true);
        totalErrors_.fetch_add(1);
        return false;
    }
    
    // Get the client handle from the request we sent
    UA_UInt32 clientHandle = getNextClientHandle() - 1; // We just incremented it in createMonitoredItem
    
    // Store monitored item info
    MonitoredItemInfo info(nodeId, result.monitoredItemId, clientHandle);
    monitoredItems_[nodeId] = info;
    handleToNodeId_[clientHandle] = nodeId;
    
    // Mark cache entry as having subscription
    cacheManager_->setSubscriptionStatus(nodeId, true);
    
    std::ostringstream oss;
    oss << "Monitored item created for node " << nodeId 
        << " with ID: " << result.monitoredItemId 
        << ", handle: " << clientHandle;
    logActivity(oss.str());
    
    updateActivity();
    return true;
}

bool SubscriptionManager::removeMonitoredItem(const std::string& nodeId) {
    if (nodeId.empty()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    auto it = monitoredItems_.find(nodeId);
    if (it == monitoredItems_.end()) {
        logActivity("Monitored item not found for node: " + nodeId);
        return false;
    }
    
    UA_UInt32 monitoredItemId = it->second.monitoredItemId;
    UA_UInt32 clientHandle = it->second.clientHandle;
    
    bool success = deleteMonitoredItem(monitoredItemId);
    if (success) {
        // Remove from our tracking
        monitoredItems_.erase(it);
        handleToNodeId_.erase(clientHandle);
        
        // Update cache to indicate no subscription
        cacheManager_->setSubscriptionStatus(nodeId, false);
        
        std::ostringstream oss;
        oss << "Monitored item removed for node: " << nodeId;
        logActivity(oss.str());
    } else {
        std::ostringstream oss;
        oss << "Failed to remove monitored item for node: " << nodeId;
        logActivity(oss.str(), true);
        totalErrors_.fetch_add(1);
    }
    
    updateActivity();
    return success;
}

std::vector<std::string> SubscriptionManager::getActiveMonitoredItems() const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    std::vector<std::string> activeItems;
    for (const auto& pair : monitoredItems_) {
        if (pair.second.isActive) {
            activeItems.push_back(pair.first);
        }
    }
    
    return activeItems;
}

std::vector<std::string> SubscriptionManager::getAllMonitoredItems() const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    std::vector<std::string> allItems;
    for (const auto& pair : monitoredItems_) {
        allItems.push_back(pair.first);
    }
    
    return allItems;
}

bool SubscriptionManager::recreateAllMonitoredItems() {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    if (!opcClient_->isConnected()) {
        logActivity("Cannot recreate monitored items: OPC UA client not connected", true);
        return false;
    }
    
    logActivity("Recreating all monitored items after reconnection");
    
    // First, reinitialize the subscription
    subscriptionActive_.store(false);
    subscriptionId_ = 0;
    
    // Create subscription directly without calling initializeSubscription to avoid deadlock
    UA_CreateSubscriptionResponse response = createOPCSubscription();
    if (response.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        std::ostringstream oss;
        oss << "Failed to recreate subscription: " << UA_StatusCode_name(response.responseHeader.serviceResult);
        logActivity(oss.str(), true);
        return false;
    }
    
    subscriptionId_ = response.subscriptionId;
    subscriptionActive_.store(true);
    
    {
        std::ostringstream recreateOss;
        recreateOss << "Subscription recreated successfully with ID: " << subscriptionId_;
        logActivity(recreateOss.str());
    }
    
    // Get list of node IDs to recreate
    std::vector<std::string> nodeIds;
    for (const auto& pair : monitoredItems_) {
        nodeIds.push_back(pair.first);
    }
    
    // Clear current monitored items tracking
    monitoredItems_.clear();
    handleToNodeId_.clear();
    
    // Recreate each monitored item
    bool allSuccess = true;
    for (const std::string& nodeId : nodeIds) {
        UA_MonitoredItemCreateResult result = createMonitoredItem(nodeId);
        if (result.statusCode == UA_STATUSCODE_GOOD) {
            UA_UInt32 clientHandle = getNextClientHandle() - 1; // We just incremented it in createMonitoredItem
            MonitoredItemInfo info(nodeId, result.monitoredItemId, clientHandle);
            monitoredItems_[nodeId] = info;
            handleToNodeId_[clientHandle] = nodeId;
            
            // Ensure cache knows about the subscription
            cacheManager_->setSubscriptionStatus(nodeId, true);
        } else {
            std::ostringstream oss;
            oss << "Failed to recreate monitored item for node " << nodeId 
                << ": " << UA_StatusCode_name(result.statusCode);
            logActivity(oss.str(), true);
            allSuccess = false;
            totalErrors_.fetch_add(1);
        }
    }
    
    std::ostringstream oss;
    oss << "Recreated " << monitoredItems_.size() << " monitored items";
    if (!allSuccess) {
        oss << " (some failures occurred)";
    }
    logActivity(oss.str(), !allSuccess);
    
    updateActivity();
    return allSuccess;
}

size_t SubscriptionManager::cleanupUnusedItems() {
    if (!autoCleanupEnabled_.load()) {
        return 0;
    }
    
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    std::vector<std::string> itemsToRemove;
    
    for (const auto& pair : monitoredItems_) {
        if (isMonitoredItemExpired(pair.second)) {
            itemsToRemove.push_back(pair.first);
        }
    }
    
    if (itemsToRemove.empty()) {
        return 0;
    }
    
    std::ostringstream oss;
    oss << "Found " << itemsToRemove.size() << " expired monitored items to clean up";
    logActivity(oss.str());
    
    size_t removedCount = 0;
    for (const std::string& nodeId : itemsToRemove) {
        auto it = monitoredItems_.find(nodeId);
        if (it != monitoredItems_.end()) {
            UA_UInt32 monitoredItemId = it->second.monitoredItemId;
            UA_UInt32 clientHandle = it->second.clientHandle;
            
            if (deleteMonitoredItem(monitoredItemId)) {
                monitoredItems_.erase(it);
                handleToNodeId_.erase(clientHandle);
                cacheManager_->setSubscriptionStatus(nodeId, false);
                removedCount++;
                
                {
                    std::ostringstream cleanupOss;
                    cleanupOss << "Cleaned up unused monitored item for node: " << nodeId;
                    logActivity(cleanupOss.str());
                }
            } else {
                {
                    std::ostringstream errorOss;
                    errorOss << "Failed to clean up monitored item for node: " << nodeId;
                    logActivity(errorOss.str(), true);
                }
                totalErrors_.fetch_add(1);
            }
        }
    }
    
    if (removedCount > 0) {
        {
            std::ostringstream summaryOss;
            summaryOss << "Successfully cleaned up " << removedCount << " unused monitored items";
            logActivity(summaryOss.str());
        }
        updateActivity();
    }
    
    return removedCount;
}

void SubscriptionManager::updateLastAccessed(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    updateLastAccessedUnsafe(nodeId);
}

bool SubscriptionManager::hasMonitoredItem(const std::string& nodeId) const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    auto it = monitoredItems_.find(nodeId);
    return it != monitoredItems_.end() && it->second.isActive;
}

SubscriptionManager::SubscriptionStats SubscriptionManager::getStats() const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    SubscriptionStats stats;
    stats.subscriptionId = subscriptionId_;
    stats.totalMonitoredItems = monitoredItems_.size();
    stats.activeMonitoredItems = 0;
    stats.inactiveMonitoredItems = 0;
    
    for (const auto& pair : monitoredItems_) {
        if (pair.second.isActive) {
            stats.activeMonitoredItems++;
        } else {
            stats.inactiveMonitoredItems++;
        }
    }
    
    stats.totalNotifications = totalNotifications_.load();
    stats.totalErrors = totalErrors_.load();
    stats.creationTime = creationTime_;
    stats.lastActivity = lastActivity_.load();
    stats.isSubscriptionActive = subscriptionActive_.load();
    
    return stats;
}

bool SubscriptionManager::clearAllMonitoredItems() {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    logActivity("Clearing all monitored items");
    
    // Delete all monitored items
    for (const auto& pair : monitoredItems_) {
        deleteMonitoredItem(pair.second.monitoredItemId);
        cacheManager_->setSubscriptionStatus(pair.first, false);
    }
    
    // Clear tracking
    monitoredItems_.clear();
    handleToNodeId_.clear();
    
    // Reset subscription
    subscriptionId_ = 0;
    subscriptionActive_.store(false);
    
    logActivity("All monitored items cleared");
    updateActivity();
    return true;
}

bool SubscriptionManager::isSubscriptionActive() const {
    return subscriptionActive_.load();
}

UA_UInt32 SubscriptionManager::getSubscriptionId() const {
    return subscriptionId_;
}

void SubscriptionManager::setItemExpireTime(int minutes) {
    itemExpireTime_ = std::chrono::minutes(minutes);
    std::ostringstream oss;
    oss << "Item expire time set to " << minutes << " minutes";
    logActivity(oss.str());
}

int SubscriptionManager::getItemExpireTime() const {
    return static_cast<int>(itemExpireTime_.count());
}

void SubscriptionManager::setAutoCleanupEnabled(bool enabled) {
    autoCleanupEnabled_.store(enabled);
    std::ostringstream oss;
    oss << "Auto cleanup " << (enabled ? "enabled" : "disabled");
    logActivity(oss.str());
}

bool SubscriptionManager::isAutoCleanupEnabled() const {
    return autoCleanupEnabled_.load();
}

std::vector<std::string> SubscriptionManager::getUnusedMonitoredItems() const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    std::vector<std::string> unusedItems;
    for (const auto& pair : monitoredItems_) {
        if (isMonitoredItemExpired(pair.second)) {
            unusedItems.push_back(pair.first);
        }
    }
    
    return unusedItems;
}

std::string SubscriptionManager::getDetailedStatus() const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    std::ostringstream oss;
    oss << "=== Subscription Manager Status ===\n";
    oss << "Subscription ID: " << subscriptionId_ << "\n";
    oss << "Subscription Active: " << (subscriptionActive_.load() ? "Yes" : "No") << "\n";
    oss << "Total Monitored Items: " << monitoredItems_.size() << "\n";
    
    size_t activeCount = 0;
    size_t expiredCount = 0;
    for (const auto& pair : monitoredItems_) {
        if (pair.second.isActive) {
            activeCount++;
        }
        if (isMonitoredItemExpired(pair.second)) {
            expiredCount++;
        }
    }
    
    oss << "Active Monitored Items: " << activeCount << "\n";
    oss << "Expired Monitored Items: " << expiredCount << "\n";
    oss << "Total Notifications: " << totalNotifications_.load() << "\n";
    oss << "Total Errors: " << totalErrors_.load() << "\n";
    oss << "Auto Cleanup Enabled: " << (autoCleanupEnabled_.load() ? "Yes" : "No") << "\n";
    oss << "Detailed Logging Enabled: " << (detailedLoggingEnabled_.load() ? "Yes" : "No") << "\n";
    oss << "Item Expire Time: " << itemExpireTime_.count() << " minutes\n";
    
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - creationTime_);
    oss << "Uptime: " << uptime.count() << " seconds\n";
    
    auto lastActivity = lastActivity_.load();
    auto timeSinceActivity = std::chrono::duration_cast<std::chrono::seconds>(now - lastActivity);
    oss << "Time Since Last Activity: " << timeSinceActivity.count() << " seconds\n";
    
    if (!monitoredItems_.empty()) {
        oss << "\n=== Monitored Items Details ===\n";
        for (const auto& pair : monitoredItems_) {
            const auto& info = pair.second;
            auto itemAge = std::chrono::duration_cast<std::chrono::minutes>(now - info.lastAccessed);
            oss << "Node: " << pair.first 
                << ", ID: " << info.monitoredItemId
                << ", Handle: " << info.clientHandle
                << ", Active: " << (info.isActive ? "Yes" : "No")
                << ", Age: " << itemAge.count() << " min"
                << ", Expired: " << (isMonitoredItemExpired(info) ? "Yes" : "No") << "\n";
        }
    }
    
    return oss.str();
}

void SubscriptionManager::setDetailedLoggingEnabled(bool enabled) {
    detailedLoggingEnabled_.store(enabled);
    std::ostringstream oss;
    oss << "Detailed logging " << (enabled ? "enabled" : "disabled");
    logActivity(oss.str());
}

bool SubscriptionManager::isDetailedLoggingEnabled() const {
    return detailedLoggingEnabled_.load();
}

// Static callback functions

void SubscriptionManager::dataChangeNotificationCallback(UA_Client *client, UA_UInt32 subId, 
                                                       void *subContext, UA_UInt32 monId, 
                                                       void *monContext, UA_DataValue *value) {
    (void)client;      // Suppress unused parameter warning
    (void)subId;       // Suppress unused parameter warning
    (void)monContext;  // Suppress unused parameter warning
    
    if (!subContext || !value) {
        return;
    }
    
    SubscriptionManager* manager = static_cast<SubscriptionManager*>(subContext);
    manager->handleDataChangeNotification(monId, value);
}

void SubscriptionManager::subscriptionInactivityCallback(UA_Client *client, UA_UInt32 subId, 
                                                       void *subContext) {
    (void)client;  // Suppress unused parameter warning
    (void)subId;   // Suppress unused parameter warning
    
    if (!subContext) {
        return;
    }
    
    SubscriptionManager* manager = static_cast<SubscriptionManager*>(subContext);
    manager->handleSubscriptionInactivity();
}

void SubscriptionManager::subscriptionStatusChangeCallback(UA_Client *client, UA_UInt32 subId, 
                                                         void *subContext, UA_StatusChangeNotification *notification) {
    (void)client;  // Suppress unused parameter warning
    (void)subId;   // Suppress unused parameter warning
    
    if (!subContext || !notification) {
        return;
    }
    
    SubscriptionManager* manager = static_cast<SubscriptionManager*>(subContext);
    manager->handleSubscriptionStatusChange(notification);
}

// Private methods

UA_CreateSubscriptionResponse SubscriptionManager::createOPCSubscription() {
    UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
    
    // Set subscription parameters
    request.requestedPublishingInterval = 1000.0;  // 1 second
    request.requestedLifetimeCount = 10000;        // 10000 * publishing interval
    request.requestedMaxKeepAliveCount = 10;       // 10 * publishing interval
    request.maxNotificationsPerPublish = 0;        // No limit
    request.publishingEnabled = true;
    request.priority = 0;
    
    UA_Client* client = opcClient_->getClient();
    if (!client) {
        UA_CreateSubscriptionResponse response;
        UA_CreateSubscriptionResponse_init(&response);
        response.responseHeader.serviceResult = UA_STATUSCODE_BADINTERNALERROR;
        return response;
    }
    
    return UA_Client_Subscriptions_create(client, request, 
                                        this,  // context
                                        subscriptionStatusChangeCallback,
                                        subscriptionInactivityCallback);
}

UA_MonitoredItemCreateResult SubscriptionManager::createMonitoredItem(const std::string& nodeId) {
    UA_MonitoredItemCreateResult result;
    UA_MonitoredItemCreateResult_init(&result);
    
    UA_Client* client = opcClient_->getClient();
    if (!client) {
        result.statusCode = UA_STATUSCODE_BADINTERNALERROR;
        return result;
    }
    
    // Parse node ID
    UA_NodeId nodeIdUA = UA_NODEID_NULL;
    if (nodeId.find("ns=") == 0) {
        // Parse namespace and identifier
        size_t nsEnd = nodeId.find(';');
        if (nsEnd != std::string::npos) {
            std::string nsStr = nodeId.substr(3, nsEnd - 3);
            std::string idPart = nodeId.substr(nsEnd + 1);
            
            try {
                UA_UInt16 namespaceIndex = static_cast<UA_UInt16>(std::stoi(nsStr));
                
                if (idPart.find("i=") == 0) {
                    // Numeric identifier
                    UA_UInt32 identifier = static_cast<UA_UInt32>(std::stoul(idPart.substr(2)));
                    nodeIdUA = UA_NODEID_NUMERIC(namespaceIndex, identifier);
                } else if (idPart.find("s=") == 0) {
                    // String identifier
                    std::string identifier = idPart.substr(2);
                    nodeIdUA = UA_NODEID_STRING_ALLOC(namespaceIndex, identifier.c_str());
                } else {
                    result.statusCode = UA_STATUSCODE_BADNODEIDUNKNOWN;
                    return result;
                }
            } catch (const std::exception&) {
                result.statusCode = UA_STATUSCODE_BADNODEIDUNKNOWN;
                return result;
            }
        } else {
            result.statusCode = UA_STATUSCODE_BADNODEIDUNKNOWN;
            return result;
        }
    } else {
        result.statusCode = UA_STATUSCODE_BADNODEIDUNKNOWN;
        return result;
    }
    
    // Create monitored item request
    UA_MonitoredItemCreateRequest request;
    UA_MonitoredItemCreateRequest_init(&request);
    request.itemToMonitor.nodeId = nodeIdUA;
    request.itemToMonitor.attributeId = UA_ATTRIBUTEID_VALUE;
    request.monitoringMode = UA_MONITORINGMODE_REPORTING;
    request.requestedParameters.clientHandle = getNextClientHandle();
    request.requestedParameters.samplingInterval = 1000.0;  // 1 second
    request.requestedParameters.queueSize = 1;
    request.requestedParameters.discardOldest = true;
    
    // Create the monitored item
    result = UA_Client_MonitoredItems_createDataChange(client, subscriptionId_, 
                                                     UA_TIMESTAMPSTORETURN_BOTH,
                                                     request, this,
                                                     dataChangeNotificationCallback, 
                                                     nullptr);
    
    // Clean up allocated node ID
    UA_NodeId_clear(&nodeIdUA);
    
    return result;
}

bool SubscriptionManager::deleteMonitoredItem(UA_UInt32 monitoredItemId) {
    UA_Client* client = opcClient_->getClient();
    if (!client || !subscriptionActive_.load()) {
        return false;
    }
    
    UA_DeleteMonitoredItemsRequest request;
    UA_DeleteMonitoredItemsRequest_init(&request);
    request.subscriptionId = subscriptionId_;
    request.monitoredItemIdsSize = 1;
    request.monitoredItemIds = &monitoredItemId;
    
    UA_DeleteMonitoredItemsResponse response = UA_Client_MonitoredItems_delete(client, request);
    
    bool success = (response.responseHeader.serviceResult == UA_STATUSCODE_GOOD) &&
                   (response.resultsSize > 0) &&
                   (response.results[0] == UA_STATUSCODE_GOOD);
    
    UA_DeleteMonitoredItemsResponse_clear(&response);
    
    return success;
}

void SubscriptionManager::handleDataChangeNotification(UA_UInt32 monId, const UA_DataValue* value) {
    if (!value) {
        logActivity("Received null data value in notification", true);
        totalErrors_.fetch_add(1);
        return;
    }
    
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    // Find the node ID for this monitored item
    std::string nodeId;
    for (const auto& pair : monitoredItems_) {
        if (pair.second.monitoredItemId == monId) {
            nodeId = pair.first;
            // Update last accessed time since we're receiving data for this item
            const_cast<MonitoredItemInfo&>(pair.second).lastAccessed = std::chrono::steady_clock::now();
            break;
        }
    }
    
    if (nodeId.empty()) {
        std::ostringstream oss;
        oss << "Received notification for unknown monitored item ID: " << monId;
        logActivity(oss.str(), true);
        totalErrors_.fetch_add(1);
        return;
    }
    
    // Convert to ReadResult and update cache
    ReadResult result = convertDataValueToReadResult(nodeId, value);
    
    // Update cache with the new data
    cacheManager_->updateCache(nodeId, result.value, 
                              result.success ? "Good" : "Bad",
                              result.reason, result.timestamp);
    
    totalNotifications_.fetch_add(1);
    updateActivity();
    
    // Log the notification based on logging level
    if (detailedLoggingEnabled_.load()) {
        std::ostringstream oss;
        oss << "Data change notification for node " << nodeId 
            << ": value='" << result.value << "', status=" << result.reason
            << ", timestamp=" << result.timestamp
            << ", monitoredItemId=" << monId;
        logActivity(oss.str());
    } else {
        // Basic logging
        std::ostringstream oss;
        oss << "Data updated for node " << nodeId << ": " << result.value;
        logActivity(oss.str());
    }
    
    // Check for data quality issues
    if (!result.success) {
        std::ostringstream oss;
        oss << "Data quality issue for node " << nodeId << ": " << result.reason;
        logActivity(oss.str(), true);
        totalErrors_.fetch_add(1);
    }
}

void SubscriptionManager::handleSubscriptionInactivity() {
    std::ostringstream oss;
    oss << "Subscription inactivity detected for subscription ID: " << subscriptionId_;
    logActivity(oss.str(), true);
    
    subscriptionActive_.store(false);
    totalErrors_.fetch_add(1);
    updateActivity();
    
    // If detailed logging is enabled, provide more context
    if (detailedLoggingEnabled_.load()) {
        std::ostringstream detailOss;
        detailOss << "Subscription became inactive. Active monitored items: " << monitoredItems_.size()
                  << ", Total notifications received: " << totalNotifications_.load();
        logActivity(detailOss.str());
    }
}

void SubscriptionManager::handleSubscriptionStatusChange(UA_StatusChangeNotification *notification) {
    if (!notification) {
        logActivity("Received null status change notification", true);
        totalErrors_.fetch_add(1);
        return;
    }
    
    UA_StatusCode status = notification->status;
    std::ostringstream oss;
    oss << "Subscription status changed to: " << UA_StatusCode_name(status)
        << " (0x" << std::hex << status << std::dec << ")";
    
    bool isError = (status != UA_STATUSCODE_GOOD);
    
    if (isError) {
        logActivity(oss.str(), true);
        totalErrors_.fetch_add(1);
        
        // Handle specific status codes
        switch (status) {
            case UA_STATUSCODE_BADSUBSCRIPTIONIDINVALID:
                subscriptionActive_.store(false);
                logActivity("Subscription ID is invalid - marking as inactive", true);
                break;
            case UA_STATUSCODE_BADTIMEOUT:
                logActivity("Subscription timeout detected", true);
                break;
            case UA_STATUSCODE_BADCONNECTIONCLOSED:
                subscriptionActive_.store(false);
                logActivity("Connection closed - subscription inactive", true);
                break;
            default:
                if (detailedLoggingEnabled_.load()) {
                    std::ostringstream detailOss;
                    detailOss << "Unhandled subscription status: " << UA_StatusCode_name(status);
                    logActivity(detailOss.str(), true);
                }
                break;
        }
    } else {
        logActivity(oss.str());
        // Ensure subscription is marked as active for good status
        if (!subscriptionActive_.load()) {
            subscriptionActive_.store(true);
            logActivity("Subscription reactivated", false);
        }
    }
    
    updateActivity();
}

ReadResult SubscriptionManager::convertDataValueToReadResult(const std::string& nodeId, const UA_DataValue* value) {
    if (!value) {
        return ReadResult::createError(nodeId, "Null data value");
    }
    
    // Check status
    if (value->status != UA_STATUSCODE_GOOD) {
        return ReadResult::createError(nodeId, UA_StatusCode_name(value->status));
    }
    
    // Check if value is present
    if (!value->hasValue) {
        return ReadResult::createError(nodeId, "No value present");
    }
    
    // Convert value to string
    std::string valueStr;
    const UA_Variant* variant = &value->value;
    
    if (UA_Variant_isEmpty(variant)) {
        valueStr = "";
    } else if (variant->type == &UA_TYPES[UA_TYPES_INT32]) {
        UA_Int32* intValue = static_cast<UA_Int32*>(variant->data);
        valueStr = std::to_string(*intValue);
    } else if (variant->type == &UA_TYPES[UA_TYPES_STRING]) {
        UA_String* stringValue = static_cast<UA_String*>(variant->data);
        valueStr = std::string(reinterpret_cast<char*>(stringValue->data), stringValue->length);
    } else if (variant->type == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        UA_Boolean* boolValue = static_cast<UA_Boolean*>(variant->data);
        valueStr = *boolValue ? "true" : "false";
    } else if (variant->type == &UA_TYPES[UA_TYPES_DOUBLE]) {
        UA_Double* doubleValue = static_cast<UA_Double*>(variant->data);
        valueStr = std::to_string(*doubleValue);
    } else if (variant->type == &UA_TYPES[UA_TYPES_FLOAT]) {
        UA_Float* floatValue = static_cast<UA_Float*>(variant->data);
        valueStr = std::to_string(*floatValue);
    } else {
        // For other types, use a generic representation
        valueStr = "[Unsupported type: " + std::string(variant->type->typeName) + "]";
    }
    
    // Get timestamp
    uint64_t timestamp = 0;
    if (value->hasSourceTimestamp) {
        timestamp = static_cast<uint64_t>((value->sourceTimestamp - UA_DATETIME_UNIX_EPOCH) / UA_DATETIME_MSEC);
    } else if (value->hasServerTimestamp) {
        timestamp = static_cast<uint64_t>((value->serverTimestamp - UA_DATETIME_UNIX_EPOCH) / UA_DATETIME_MSEC);
    } else {
        // Use current time
        auto now = std::chrono::system_clock::now();
        timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    }
    
    return ReadResult::createSuccess(nodeId, valueStr, timestamp);
}

UA_UInt32 SubscriptionManager::getNextClientHandle() {
    return nextClientHandle_.fetch_add(1);
}

bool SubscriptionManager::isMonitoredItemExpired(const MonitoredItemInfo& info) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - info.lastAccessed);
    return elapsed >= itemExpireTime_;
}

void SubscriptionManager::logActivity(const std::string& message, bool isError) const {
    std::string prefix = isError ? "[ERROR] " : "[INFO] ";
    std::cout << prefix << "SubscriptionManager: " << message << std::endl;
}

void SubscriptionManager::updateActivity() const {
    lastActivity_.store(std::chrono::steady_clock::now());
}

bool SubscriptionManager::validateNodeId(const std::string& nodeId) const {
    if (nodeId.empty()) {
        return false;
    }
    
    // Basic validation for ns=X;i=Y or ns=X;s=Y format
    if (nodeId.find("ns=") != 0) {
        return false;
    }
    
    size_t semicolon = nodeId.find(';');
    if (semicolon == std::string::npos) {
        return false;
    }
    
    std::string idPart = nodeId.substr(semicolon + 1);
    return (idPart.find("i=") == 0 || idPart.find("s=") == 0);
}

void SubscriptionManager::updateLastAccessedUnsafe(const std::string& nodeId) {
    auto it = monitoredItems_.find(nodeId);
    if (it != monitoredItems_.end()) {
        it->second.lastAccessed = std::chrono::steady_clock::now();
    }
}

} // namespace opcua2http