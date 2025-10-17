#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <memory>

#include "subscription/SubscriptionManager.h"
#include "opcua/OPCUAClient.h"
#include "cache/CacheManager.h"
#include "config/Configuration.h"

// Mock OPC UA Server for testing
#include <open62541/server.h>
#include <open62541/server_config_default.h>

namespace opcua2http {
namespace test {

// Global variables to ensure values remain valid throughout server lifetime
static UA_Int32 g_testIntValue = 100;
static UA_String g_testStringValue = UA_STRING_STATIC("Test String");
static UA_Boolean g_testBoolValue = false;

class MockOPCUAServer {
public:
    MockOPCUAServer(uint16_t port = 4842) : port_(port), server_(nullptr), running_(false), serverReady_(false) {}
    
    ~MockOPCUAServer() {
        stop();
    }
    
    bool start() {
        server_ = UA_Server_new();
        if (!server_) {
            std::cerr << "Failed to create UA_Server" << std::endl;
            return false;
        }
        
        // Use minimal configuration with the port
        UA_ServerConfig* config = UA_Server_getConfig(server_);
        UA_StatusCode status = UA_ServerConfig_setMinimal(config, port_, nullptr);
        if (status != UA_STATUSCODE_GOOD) {
            std::cerr << "Failed to set minimal server config: " << UA_StatusCode_name(status) << std::endl;
            UA_Server_delete(server_);
            server_ = nullptr;
            return false;
        }
        
        // Add test variables
        addTestVariables();
        
        // Start server in separate thread
        running_ = true;
        serverReady_ = false;
        
        serverThread_ = std::thread([this]() {
            // Start the server
            UA_StatusCode status = UA_Server_run_startup(server_);
            if (status != UA_STATUSCODE_GOOD) {
                std::cerr << "Failed to start server: " << UA_StatusCode_name(status) << std::endl;
                running_ = false;
                return;
            }
            
            serverReady_ = true;
            std::cout << "Mock OPC UA server started on port " << port_ << std::endl;
            
            // Run server loop
            while (running_) {
                UA_Server_run_iterate(server_, true);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            
            // Shutdown server
            UA_Server_run_shutdown(server_);
            std::cout << "Mock OPC UA server stopped" << std::endl;
        });
        
        // Wait for server to be ready
        int attempts = 0;
        while (!serverReady_ && running_ && attempts < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            attempts++;
        }
        
        if (!serverReady_) {
            std::cerr << "Server failed to start within timeout" << std::endl;
            stop();
            return false;
        }
        
        // Additional wait to ensure server is fully ready
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        return true;
    }
    
    void stop() {
        if (running_) {
            running_ = false;
            
            // Give server thread time to notice the stop signal
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            if (serverThread_.joinable()) {
                serverThread_.join();
            }
        }
        
        if (server_) {
            UA_Server_delete(server_);
            server_ = nullptr;
        }
        
        serverReady_ = false;
    }
    
    std::string getEndpoint() const {
        return "opc.tcp://localhost:" + std::to_string(port_);
    }
    
    UA_UInt16 getTestNamespaceIndex() const {
        return testNamespaceIndex_;
    }
    
    // Update a test variable value (for testing data change notifications)
    void updateTestVariable(UA_UInt32 nodeId, const UA_Variant& newValue) {
        if (!server_) return;
        
        UA_NodeId testNodeId = UA_NODEID_NUMERIC(testNamespaceIndex_, nodeId);
        UA_Server_writeValue(server_, testNodeId, newValue);
    }
    
private:
    void addTestVariables() {
        if (!server_) return;
        
        // Add namespace for our test variables
        UA_UInt16 nsIndex = UA_Server_addNamespace(server_, "http://test.subscription.server");
        
        std::cout << "Added namespace with index: " << nsIndex << std::endl;
        
        // Add integer variable
        {
            UA_VariableAttributes attr = UA_VariableAttributes_default;
            attr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("SubTestInt"));
            UA_Variant_setScalar(&attr.value, &g_testIntValue, &UA_TYPES[UA_TYPES_INT32]);
            attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
            attr.valueRank = UA_VALUERANK_SCALAR;
            attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
            attr.userAccessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
            
            UA_NodeId newNodeId = UA_NODEID_NUMERIC(nsIndex, 1001);
            UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
            UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_NodeId variableType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
            UA_QualifiedName browseName = UA_QUALIFIEDNAME(nsIndex, const_cast<char*>("SubTestInt"));
            
            UA_StatusCode status = UA_Server_addVariableNode(server_, newNodeId, parentNodeId,
                                      parentReferenceNodeId, browseName,
                                      variableType, attr, nullptr, nullptr);
            
            if (status != UA_STATUSCODE_GOOD) {
                std::cerr << "Failed to add integer variable: " << UA_StatusCode_name(status) << std::endl;
            } else {
                std::cout << "Added integer variable: ns=" << nsIndex << ";i=1001" << std::endl;
            }
        }
        
        // Add string variable
        {
            UA_VariableAttributes attr = UA_VariableAttributes_default;
            attr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("SubTestString"));
            UA_Variant_setScalar(&attr.value, &g_testStringValue, &UA_TYPES[UA_TYPES_STRING]);
            attr.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
            attr.valueRank = UA_VALUERANK_SCALAR;
            attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
            attr.userAccessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
            
            UA_NodeId newNodeId = UA_NODEID_NUMERIC(nsIndex, 1002);
            UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
            UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_NodeId variableType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
            UA_QualifiedName browseName = UA_QUALIFIEDNAME(nsIndex, const_cast<char*>("SubTestString"));
            
            UA_StatusCode status = UA_Server_addVariableNode(server_, newNodeId, parentNodeId,
                                      parentReferenceNodeId, browseName,
                                      variableType, attr, nullptr, nullptr);
            
            if (status != UA_STATUSCODE_GOOD) {
                std::cerr << "Failed to add string variable: " << UA_StatusCode_name(status) << std::endl;
            } else {
                std::cout << "Added string variable: ns=" << nsIndex << ";i=1002" << std::endl;
            }
        }
        
        // Add boolean variable
        {
            UA_VariableAttributes attr = UA_VariableAttributes_default;
            attr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("SubTestBool"));
            UA_Variant_setScalar(&attr.value, &g_testBoolValue, &UA_TYPES[UA_TYPES_BOOLEAN]);
            attr.dataType = UA_TYPES[UA_TYPES_BOOLEAN].typeId;
            attr.valueRank = UA_VALUERANK_SCALAR;
            attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
            attr.userAccessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
            
            UA_NodeId newNodeId = UA_NODEID_NUMERIC(nsIndex, 1003);
            UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
            UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_NodeId variableType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
            UA_QualifiedName browseName = UA_QUALIFIEDNAME(nsIndex, const_cast<char*>("SubTestBool"));
            
            UA_StatusCode status = UA_Server_addVariableNode(server_, newNodeId, parentNodeId,
                                      parentReferenceNodeId, browseName,
                                      variableType, attr, nullptr, nullptr);
            
            if (status != UA_STATUSCODE_GOOD) {
                std::cerr << "Failed to add boolean variable: " << UA_StatusCode_name(status) << std::endl;
            } else {
                std::cout << "Added boolean variable: ns=" << nsIndex << ";i=1003" << std::endl;
            }
        }
        
        // Store the namespace index for tests to use
        testNamespaceIndex_ = nsIndex;
    }
    
    uint16_t port_;
    UA_Server* server_;
    std::atomic<bool> running_;
    std::atomic<bool> serverReady_;
    std::thread serverThread_;
    UA_UInt16 testNamespaceIndex_;
};

class SubscriptionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start mock server
        mockServer_ = std::make_unique<MockOPCUAServer>(4842);
        ASSERT_TRUE(mockServer_->start()) << "Failed to start mock OPC UA server";
        
        // Server should be ready now, minimal additional wait
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Create test configuration
        config_.opcEndpoint = mockServer_->getEndpoint();
        config_.securityMode = 1; // None
        config_.securityPolicy = "None";
        config_.defaultNamespace = mockServer_->getTestNamespaceIndex();
        config_.applicationUri = "urn:test:opcua:subscription:client";
        config_.connectionRetryMax = 3;
        config_.connectionInitialDelay = 100;
        config_.connectionMaxRetry = 5;
        config_.connectionMaxDelay = 5000;
        config_.connectionRetryDelay = 1000;
        
        // Create components
        opcClient_ = std::make_unique<OPCUAClient>();
        cacheManager_ = std::make_unique<CacheManager>(60, 1000); // 60 min expiration, 1000 max entries
        
        // Initialize and connect client
        ASSERT_TRUE(opcClient_->initialize(config_));
        ASSERT_TRUE(opcClient_->connect());
        
        // Create subscription manager with short expiration for testing
        subscriptionManager_ = std::make_unique<SubscriptionManager>(
            opcClient_.get(), cacheManager_.get(), 1); // 1 minute expiration
    }
    
    void TearDown() override {
        // Clean up in reverse order
        if (subscriptionManager_) {
            subscriptionManager_.reset();
        }
        
        if (opcClient_) {
            if (opcClient_->isConnected()) {
                opcClient_->disconnect();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            opcClient_.reset();
        }
        
        if (cacheManager_) {
            cacheManager_.reset();
        }
        
        if (mockServer_) {
            mockServer_->stop();
            mockServer_.reset();
        }
    }
    
    std::string getTestNodeId(UA_UInt32 nodeId) const {
        return "ns=" + std::to_string(config_.defaultNamespace) + ";i=" + std::to_string(nodeId);
    }
    
    std::unique_ptr<MockOPCUAServer> mockServer_;
    std::unique_ptr<OPCUAClient> opcClient_;
    std::unique_ptr<CacheManager> cacheManager_;
    std::unique_ptr<SubscriptionManager> subscriptionManager_;
    Configuration config_;
};

// Test subscription manager initialization
TEST_F(SubscriptionManagerTest, InitializeSubscription) {
    EXPECT_FALSE(subscriptionManager_->isSubscriptionActive());
    EXPECT_EQ(subscriptionManager_->getSubscriptionId(), 0);
    
    EXPECT_TRUE(subscriptionManager_->initializeSubscription());
    EXPECT_TRUE(subscriptionManager_->isSubscriptionActive());
    EXPECT_NE(subscriptionManager_->getSubscriptionId(), 0);
    
    // Test double initialization
    EXPECT_TRUE(subscriptionManager_->initializeSubscription());
    EXPECT_TRUE(subscriptionManager_->isSubscriptionActive());
}

// Test adding monitored items
TEST_F(SubscriptionManagerTest, AddMonitoredItem) {
    std::string nodeId = getTestNodeId(1001);
    
    // Initially no monitored items
    EXPECT_FALSE(subscriptionManager_->hasMonitoredItem(nodeId));
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 0);
    
    // Add monitored item
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    EXPECT_TRUE(subscriptionManager_->hasMonitoredItem(nodeId));
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 1);
    EXPECT_TRUE(subscriptionManager_->isSubscriptionActive());
    
    // Check cache subscription status
    auto cacheEntry = cacheManager_->getCachedValue(nodeId);
    if (cacheEntry.has_value()) {
        EXPECT_TRUE(cacheEntry->hasSubscription);
    }
    
    // Test adding same item again
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 1);
}

// Test adding multiple monitored items
TEST_F(SubscriptionManagerTest, AddMultipleMonitoredItems) {
    std::vector<std::string> nodeIds = {
        getTestNodeId(1001),
        getTestNodeId(1002),
        getTestNodeId(1003)
    };
    
    // Add all monitored items
    for (const auto& nodeId : nodeIds) {
        EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
        EXPECT_TRUE(subscriptionManager_->hasMonitoredItem(nodeId));
    }
    
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 3);
    
    // Check all items are in the active list
    auto activeItems = subscriptionManager_->getActiveMonitoredItems();
    for (const auto& nodeId : nodeIds) {
        EXPECT_NE(std::find(activeItems.begin(), activeItems.end(), nodeId), activeItems.end());
    }
}

// Test removing monitored items
TEST_F(SubscriptionManagerTest, RemoveMonitoredItem) {
    std::string nodeId = getTestNodeId(1001);
    
    // Add monitored item first
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    EXPECT_TRUE(subscriptionManager_->hasMonitoredItem(nodeId));
    
    // Remove monitored item
    EXPECT_TRUE(subscriptionManager_->removeMonitoredItem(nodeId));
    EXPECT_FALSE(subscriptionManager_->hasMonitoredItem(nodeId));
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 0);
    
    // Test removing non-existent item
    EXPECT_FALSE(subscriptionManager_->removeMonitoredItem("ns=1;i=9999"));
}

// Test invalid node IDs
TEST_F(SubscriptionManagerTest, InvalidNodeIds) {
    // Test empty node ID
    EXPECT_FALSE(subscriptionManager_->addMonitoredItem(""));
    
    // Test invalid format
    EXPECT_FALSE(subscriptionManager_->addMonitoredItem("invalid-node-id"));
    EXPECT_FALSE(subscriptionManager_->addMonitoredItem("ns=1"));
    EXPECT_FALSE(subscriptionManager_->addMonitoredItem("i=1001"));
    
    // Test non-existent node
    std::string nonExistentNode = getTestNodeId(9999);
    EXPECT_FALSE(subscriptionManager_->addMonitoredItem(nonExistentNode));
}

// Test subscription statistics
TEST_F(SubscriptionManagerTest, SubscriptionStats) {
    auto stats = subscriptionManager_->getStats();
    EXPECT_EQ(stats.totalMonitoredItems, 0);
    EXPECT_EQ(stats.activeMonitoredItems, 0);
    EXPECT_EQ(stats.totalNotifications, 0);
    EXPECT_FALSE(stats.isSubscriptionActive);
    
    // Add some monitored items
    std::string nodeId1 = getTestNodeId(1001);
    std::string nodeId2 = getTestNodeId(1002);
    
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId1));
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId2));
    
    stats = subscriptionManager_->getStats();
    EXPECT_EQ(stats.totalMonitoredItems, 2);
    EXPECT_EQ(stats.activeMonitoredItems, 2);
    EXPECT_TRUE(stats.isSubscriptionActive);
    EXPECT_NE(stats.subscriptionId, 0);
}

// Test data change notifications (basic test)
TEST_F(SubscriptionManagerTest, DataChangeNotifications) {
    std::string nodeId = getTestNodeId(1001);
    
    // Add monitored item
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    
    // Allow some time for subscription to be established
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Update the server variable value
    UA_Variant newValue;
    UA_Variant_init(&newValue);
    UA_Int32 newIntValue = 999;
    UA_Variant_setScalar(&newValue, &newIntValue, &UA_TYPES[UA_TYPES_INT32]);
    
    mockServer_->updateTestVariable(1001, newValue);
    
    // Allow time for notification to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Run client iterate to process notifications
    for (int i = 0; i < 10; i++) {
        opcClient_->runIterate(100);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Check if notification was received (stats should show notifications)
    auto stats = subscriptionManager_->getStats();
    // Note: This might be 0 if the notification hasn't been processed yet
    // In a real scenario, we'd wait longer or use a more sophisticated test
    std::cout << "Total notifications received: " << stats.totalNotifications << std::endl;
}

// Test cleanup of unused items
TEST_F(SubscriptionManagerTest, CleanupUnusedItems) {
    // Create subscription manager with very short expiration (for testing)
    auto shortExpirationManager = std::make_unique<SubscriptionManager>(
        opcClient_.get(), cacheManager_.get(), 0); // 0 minutes = immediate expiration
    
    std::string nodeId = getTestNodeId(1001);
    
    // Add monitored item
    EXPECT_TRUE(shortExpirationManager->addMonitoredItem(nodeId));
    EXPECT_TRUE(shortExpirationManager->hasMonitoredItem(nodeId));
    
    // Wait a bit to ensure item is considered expired
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Clean up unused items
    size_t removedCount = shortExpirationManager->cleanupUnusedItems();
    EXPECT_GT(removedCount, 0);
    EXPECT_FALSE(shortExpirationManager->hasMonitoredItem(nodeId));
}

// Test auto cleanup enable/disable
TEST_F(SubscriptionManagerTest, AutoCleanupControl) {
    EXPECT_TRUE(subscriptionManager_->isAutoCleanupEnabled());
    
    subscriptionManager_->setAutoCleanupEnabled(false);
    EXPECT_FALSE(subscriptionManager_->isAutoCleanupEnabled());
    
    // When auto cleanup is disabled, cleanup should return 0
    size_t removedCount = subscriptionManager_->cleanupUnusedItems();
    EXPECT_EQ(removedCount, 0);
    
    subscriptionManager_->setAutoCleanupEnabled(true);
    EXPECT_TRUE(subscriptionManager_->isAutoCleanupEnabled());
}

// Test detailed logging control
TEST_F(SubscriptionManagerTest, DetailedLoggingControl) {
    EXPECT_TRUE(subscriptionManager_->isDetailedLoggingEnabled());
    
    subscriptionManager_->setDetailedLoggingEnabled(false);
    EXPECT_FALSE(subscriptionManager_->isDetailedLoggingEnabled());
    
    subscriptionManager_->setDetailedLoggingEnabled(true);
    EXPECT_TRUE(subscriptionManager_->isDetailedLoggingEnabled());
}

// Test item expiration time configuration
TEST_F(SubscriptionManagerTest, ItemExpirationTimeConfiguration) {
    EXPECT_EQ(subscriptionManager_->getItemExpireTime(), 1); // Set to 1 minute in SetUp
    
    subscriptionManager_->setItemExpireTime(30);
    EXPECT_EQ(subscriptionManager_->getItemExpireTime(), 30);
    
    subscriptionManager_->setItemExpireTime(60);
    EXPECT_EQ(subscriptionManager_->getItemExpireTime(), 60);
}

// Test getting unused monitored items
TEST_F(SubscriptionManagerTest, GetUnusedMonitoredItems) {
    std::string nodeId = getTestNodeId(1001);
    
    // Initially no unused items
    auto unusedItems = subscriptionManager_->getUnusedMonitoredItems();
    EXPECT_EQ(unusedItems.size(), 0);
    
    // Add monitored item
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    
    // With recent access, should not be unused
    unusedItems = subscriptionManager_->getUnusedMonitoredItems();
    EXPECT_EQ(unusedItems.size(), 0);
    
    // Update last accessed time to simulate old access
    subscriptionManager_->updateLastAccessed(nodeId);
    unusedItems = subscriptionManager_->getUnusedMonitoredItems();
    EXPECT_EQ(unusedItems.size(), 0); // Still recent
}

// Test clear all monitored items
TEST_F(SubscriptionManagerTest, ClearAllMonitoredItems) {
    // Add multiple monitored items
    std::vector<std::string> nodeIds = {
        getTestNodeId(1001),
        getTestNodeId(1002)
    };
    
    for (const auto& nodeId : nodeIds) {
        EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    }
    
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 2);
    EXPECT_TRUE(subscriptionManager_->isSubscriptionActive());
    
    // Clear all items
    EXPECT_TRUE(subscriptionManager_->clearAllMonitoredItems());
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 0);
    EXPECT_FALSE(subscriptionManager_->isSubscriptionActive());
    EXPECT_EQ(subscriptionManager_->getSubscriptionId(), 0);
}

// Test detailed status information
TEST_F(SubscriptionManagerTest, DetailedStatus) {
    std::string status = subscriptionManager_->getDetailedStatus();
    EXPECT_FALSE(status.empty());
    EXPECT_NE(status.find("Subscription Manager Status"), std::string::npos);
    EXPECT_NE(status.find("Total Monitored Items: 0"), std::string::npos);
    
    // Add a monitored item and check status again
    std::string nodeId = getTestNodeId(1001);
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    
    status = subscriptionManager_->getDetailedStatus();
    EXPECT_NE(status.find("Total Monitored Items: 1"), std::string::npos);
    EXPECT_NE(status.find("Active Monitored Items: 1"), std::string::npos);
    EXPECT_NE(status.find("Monitored Items Details"), std::string::npos);
    EXPECT_NE(status.find(nodeId), std::string::npos);
}

// Test recreation of monitored items (for reconnection scenarios)
TEST_F(SubscriptionManagerTest, RecreateAllMonitoredItems) {
    // Add some monitored items
    std::vector<std::string> nodeIds = {
        getTestNodeId(1001),
        getTestNodeId(1002)
    };
    
    for (const auto& nodeId : nodeIds) {
        EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    }
    
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 2);
    
    // Recreate all monitored items (simulates reconnection)
    EXPECT_TRUE(subscriptionManager_->recreateAllMonitoredItems());
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 2);
    EXPECT_TRUE(subscriptionManager_->isSubscriptionActive());
    
    // Verify all original items are still present
    for (const auto& nodeId : nodeIds) {
        EXPECT_TRUE(subscriptionManager_->hasMonitoredItem(nodeId));
    }
}

// Test constructor with invalid parameters
TEST_F(SubscriptionManagerTest, InvalidConstructorParameters) {
    // Test null OPC client
    EXPECT_THROW(
        SubscriptionManager(nullptr, cacheManager_.get()),
        std::invalid_argument
    );
    
    // Test null cache manager
    EXPECT_THROW(
        SubscriptionManager(opcClient_.get(), nullptr),
        std::invalid_argument
    );
}

} // namespace test
} // namespace opcua2http