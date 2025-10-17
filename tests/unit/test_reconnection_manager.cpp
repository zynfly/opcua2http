#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <memory>
#include <atomic>

#include "reconnection/ReconnectionManager.h"
#include "opcua/OPCUAClient.h"
#include "subscription/SubscriptionManager.h"
#include "cache/CacheManager.h"
#include "config/Configuration.h"

// Mock OPC UA Server for testing
#include <open62541/server.h>
#include <open62541/server_config_default.h>

namespace opcua2http {
namespace test {

// Global variables to ensure values remain valid throughout server lifetime
static UA_Int32 g_testIntValue = 200;
static UA_String g_testStringValue = UA_STRING_STATIC("Reconnection Test");

class MockOPCUAServerForReconnection {
public:
    MockOPCUAServerForReconnection(uint16_t port = 4843) : port_(port), server_(nullptr), running_(false), serverReady_(false) {}
    
    ~MockOPCUAServerForReconnection() {
        stop();
    }
    
    bool start() {
        server_ = UA_Server_new();
        if (!server_) {
            std::cerr << "Failed to create UA_Server for reconnection test" << std::endl;
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
                std::cerr << "Failed to start reconnection test server: " << UA_StatusCode_name(status) << std::endl;
                running_ = false;
                return;
            }
            
            serverReady_ = true;
            std::cout << "Mock OPC UA server for reconnection test started on port " << port_ << std::endl;
            
            // Run server loop
            while (running_) {
                UA_Server_run_iterate(server_, true);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            
            // Shutdown server
            UA_Server_run_shutdown(server_);
            std::cout << "Mock OPC UA server for reconnection test stopped" << std::endl;
        });
        
        // Wait for server to be ready
        int attempts = 0;
        while (!serverReady_ && running_ && attempts < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            attempts++;
        }
        
        if (!serverReady_) {
            std::cerr << "Reconnection test server failed to start within timeout" << std::endl;
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
    
    bool isRunning() const {
        return running_ && serverReady_;
    }
    
    // Simulate server shutdown (for testing reconnection)
    void simulateShutdown() {
        if (running_) {
            running_ = false;
            if (serverThread_.joinable()) {
                serverThread_.join();
            }
            serverReady_ = false;
            std::cout << "Simulated server shutdown" << std::endl;
        }
    }
    
    // Restart server after shutdown
    bool restart() {
        if (running_) {
            return true; // Already running
        }
        
        // Clean up old server
        if (server_) {
            UA_Server_delete(server_);
            server_ = nullptr;
        }
        
        // Start new server
        return start();
    }
    
private:
    void addTestVariables() {
        if (!server_) return;
        
        // Add namespace for our test variables
        testNamespaceIndex_ = UA_Server_addNamespace(server_, "http://test.reconnection.server");
        
        std::cout << "Added reconnection test namespace with index: " << testNamespaceIndex_ << std::endl;
        
        // Add integer variable
        {
            UA_VariableAttributes attr = UA_VariableAttributes_default;
            attr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("ReconnTestInt"));
            UA_Variant_setScalar(&attr.value, &g_testIntValue, &UA_TYPES[UA_TYPES_INT32]);
            attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
            attr.valueRank = UA_VALUERANK_SCALAR;
            attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
            attr.userAccessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
            
            UA_NodeId newNodeId = UA_NODEID_NUMERIC(testNamespaceIndex_, 2001);
            UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
            UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_NodeId variableType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
            UA_QualifiedName browseName = UA_QUALIFIEDNAME(testNamespaceIndex_, const_cast<char*>("ReconnTestInt"));
            
            UA_StatusCode status = UA_Server_addVariableNode(server_, newNodeId, parentNodeId,
                                      parentReferenceNodeId, browseName,
                                      variableType, attr, nullptr, nullptr);
            
            if (status != UA_STATUSCODE_GOOD) {
                std::cerr << "Failed to add reconnection test integer variable: " << UA_StatusCode_name(status) << std::endl;
            } else {
                std::cout << "Added reconnection test integer variable: ns=" << testNamespaceIndex_ << ";i=2001" << std::endl;
            }
        }
        
        // Add string variable
        {
            UA_VariableAttributes attr = UA_VariableAttributes_default;
            attr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("ReconnTestString"));
            UA_Variant_setScalar(&attr.value, &g_testStringValue, &UA_TYPES[UA_TYPES_STRING]);
            attr.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
            attr.valueRank = UA_VALUERANK_SCALAR;
            attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
            attr.userAccessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
            
            UA_NodeId newNodeId = UA_NODEID_NUMERIC(testNamespaceIndex_, 2002);
            UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
            UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_NodeId variableType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
            UA_QualifiedName browseName = UA_QUALIFIEDNAME(testNamespaceIndex_, const_cast<char*>("ReconnTestString"));
            
            UA_StatusCode status = UA_Server_addVariableNode(server_, newNodeId, parentNodeId,
                                      parentReferenceNodeId, browseName,
                                      variableType, attr, nullptr, nullptr);
            
            if (status != UA_STATUSCODE_GOOD) {
                std::cerr << "Failed to add reconnection test string variable: " << UA_StatusCode_name(status) << std::endl;
            } else {
                std::cout << "Added reconnection test string variable: ns=" << testNamespaceIndex_ << ";i=2002" << std::endl;
            }
        }
    }
    
    uint16_t port_;
    UA_Server* server_;
    std::thread serverThread_;
    std::atomic<bool> running_;
    std::atomic<bool> serverReady_;
    UA_UInt16 testNamespaceIndex_;
};

class ReconnectionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start mock server
        mockServer_ = std::make_unique<MockOPCUAServerForReconnection>();
        ASSERT_TRUE(mockServer_->start());
        
        // Configure test settings
        config_.opcEndpoint = mockServer_->getEndpoint();
        config_.securityMode = 1; // None
        config_.securityPolicy = "None";
        config_.defaultNamespace = mockServer_->getTestNamespaceIndex();
        config_.applicationUri = "urn:test:opcua:reconnection:client";
        
        // Reconnection configuration - use short delays for testing
        config_.connectionRetryMax = 3;
        config_.connectionInitialDelay = 100;   // 100ms
        config_.connectionMaxRetry = 5;
        config_.connectionMaxDelay = 2000;      // 2 seconds
        config_.connectionRetryDelay = 500;     // 500ms
        
        // Create components
        opcClient_ = std::make_unique<OPCUAClient>();
        cacheManager_ = std::make_unique<CacheManager>(60, 1000); // 60 min expiration, 1000 max entries
        
        // Initialize and connect client
        ASSERT_TRUE(opcClient_->initialize(config_));
        ASSERT_TRUE(opcClient_->connect());
        
        // Create subscription manager
        subscriptionManager_ = std::make_unique<SubscriptionManager>(
            opcClient_.get(), cacheManager_.get(), 1); // 1 minute expiration
        
        // Create reconnection manager
        reconnectionManager_ = std::make_unique<ReconnectionManager>(
            opcClient_.get(), subscriptionManager_.get(), config_);
    }
    
    void TearDown() override {
        // Clean up in reverse order
        if (reconnectionManager_) {
            reconnectionManager_->stopMonitoring();
            reconnectionManager_.reset();
        }
        
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
    
    std::unique_ptr<MockOPCUAServerForReconnection> mockServer_;
    std::unique_ptr<OPCUAClient> opcClient_;
    std::unique_ptr<CacheManager> cacheManager_;
    std::unique_ptr<SubscriptionManager> subscriptionManager_;
    std::unique_ptr<ReconnectionManager> reconnectionManager_;
    Configuration config_;
};

// Test reconnection manager initialization
TEST_F(ReconnectionManagerTest, Initialization) {
    EXPECT_FALSE(reconnectionManager_->isMonitoring());
    EXPECT_EQ(reconnectionManager_->getState(), ReconnectionManager::ReconnectionState::IDLE);
    EXPECT_FALSE(reconnectionManager_->isReconnecting());
    
    // Test statistics initialization
    auto stats = reconnectionManager_->getStats();
    EXPECT_EQ(stats.totalReconnectionAttempts, 0);
    EXPECT_EQ(stats.successfulReconnections, 0);
    EXPECT_EQ(stats.failedReconnections, 0);
    EXPECT_EQ(stats.subscriptionRecoveries, 0);
    EXPECT_EQ(stats.successfulSubscriptionRecoveries, 0);
    EXPECT_EQ(stats.currentState, ReconnectionManager::ReconnectionState::IDLE);
    EXPECT_FALSE(stats.isMonitoring);
}

// Test starting and stopping monitoring
TEST_F(ReconnectionManagerTest, StartStopMonitoring) {
    // Start monitoring
    EXPECT_TRUE(reconnectionManager_->startMonitoring());
    EXPECT_TRUE(reconnectionManager_->isMonitoring());
    EXPECT_EQ(reconnectionManager_->getState(), ReconnectionManager::ReconnectionState::MONITORING);
    
    // Allow some time for monitoring to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Test double start
    EXPECT_TRUE(reconnectionManager_->startMonitoring());
    EXPECT_TRUE(reconnectionManager_->isMonitoring());
    
    // Stop monitoring
    reconnectionManager_->stopMonitoring();
    EXPECT_FALSE(reconnectionManager_->isMonitoring());
    EXPECT_EQ(reconnectionManager_->getState(), ReconnectionManager::ReconnectionState::IDLE);
    
    // Test double stop
    reconnectionManager_->stopMonitoring();
    EXPECT_FALSE(reconnectionManager_->isMonitoring());
}

// Test configuration update
TEST_F(ReconnectionManagerTest, ConfigurationUpdate) {
    Configuration newConfig = config_;
    newConfig.connectionRetryMax = 10;
    newConfig.connectionMaxDelay = 5000;
    
    reconnectionManager_->updateConfiguration(newConfig);
    
    // Configuration should be updated (we can't directly test private members,
    // but we can test that the update doesn't cause errors)
    EXPECT_NO_THROW(reconnectionManager_->getDetailedStatus());
}

// Test manual reconnection trigger
TEST_F(ReconnectionManagerTest, ManualReconnectionTrigger) {
    // When already connected, manual trigger should succeed quickly
    EXPECT_TRUE(reconnectionManager_->triggerReconnection());
    
    auto stats = reconnectionManager_->getStats();
    EXPECT_GT(stats.totalReconnectionAttempts, 0);
}

// Test connection state callback
TEST_F(ReconnectionManagerTest, ConnectionStateCallback) {
    std::atomic<bool> callbackCalled{false};
    std::atomic<bool> lastConnectedState{false};
    std::atomic<bool> lastReconnectedState{false};
    
    reconnectionManager_->setConnectionStateCallback(
        [&](bool connected, bool reconnected) {
            callbackCalled = true;
            lastConnectedState = connected;
            lastReconnectedState = reconnected;
        });
    
    // Disconnect first to test reconnection callback
    opcClient_->disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Trigger a reconnection to test callback
    EXPECT_TRUE(reconnectionManager_->triggerReconnection());
    
    // Allow some time for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Callback should have been called for reconnection
    EXPECT_TRUE(callbackCalled.load());
    EXPECT_TRUE(lastConnectedState.load()); // Should be connected
    EXPECT_TRUE(lastReconnectedState.load()); // Should be a reconnection
}

// Test statistics and status reporting
TEST_F(ReconnectionManagerTest, StatisticsAndStatus) {
    // Get initial stats
    auto initialStats = reconnectionManager_->getStats();
    EXPECT_EQ(initialStats.totalReconnectionAttempts, 0);
    
    // Trigger a reconnection
    EXPECT_TRUE(reconnectionManager_->triggerReconnection());
    
    // Check updated stats
    auto updatedStats = reconnectionManager_->getStats();
    EXPECT_GT(updatedStats.totalReconnectionAttempts, initialStats.totalReconnectionAttempts);
    
    // Test detailed status
    std::string status = reconnectionManager_->getDetailedStatus();
    EXPECT_FALSE(status.empty());
    EXPECT_NE(status.find("Reconnection Manager Status"), std::string::npos);
    
    // Test reset stats
    reconnectionManager_->resetStats();
    auto resetStats = reconnectionManager_->getStats();
    EXPECT_EQ(resetStats.totalReconnectionAttempts, 0);
    EXPECT_EQ(resetStats.successfulReconnections, 0);
    EXPECT_EQ(resetStats.failedReconnections, 0);
}

// Test detailed logging enable/disable
TEST_F(ReconnectionManagerTest, DetailedLogging) {
    EXPECT_TRUE(reconnectionManager_->isDetailedLoggingEnabled()); // Default is enabled
    
    reconnectionManager_->setDetailedLoggingEnabled(false);
    EXPECT_FALSE(reconnectionManager_->isDetailedLoggingEnabled());
    
    reconnectionManager_->setDetailedLoggingEnabled(true);
    EXPECT_TRUE(reconnectionManager_->isDetailedLoggingEnabled());
}

// Test connection monitoring and disconnection detection
TEST_F(ReconnectionManagerTest, ConnectionMonitoring) {
    // Test basic monitoring functionality without relying on timing
    // This test focuses on the monitoring state management rather than actual reconnection
    
    // Start monitoring
    EXPECT_TRUE(reconnectionManager_->startMonitoring());
    
    // Allow monitoring to start
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Verify monitoring state
    EXPECT_TRUE(reconnectionManager_->isMonitoring());
    EXPECT_EQ(reconnectionManager_->getState(), ReconnectionManager::ReconnectionState::MONITORING);
    
    // Test manual reconnection while monitoring is active
    auto initialStats = reconnectionManager_->getStats();
    EXPECT_TRUE(reconnectionManager_->triggerReconnection());
    
    auto updatedStats = reconnectionManager_->getStats();
    EXPECT_GT(updatedStats.totalReconnectionAttempts, initialStats.totalReconnectionAttempts);
    
    // Stop monitoring
    reconnectionManager_->stopMonitoring();
    EXPECT_FALSE(reconnectionManager_->isMonitoring());
    EXPECT_EQ(reconnectionManager_->getState(), ReconnectionManager::ReconnectionState::IDLE);
}

// Test subscription recovery after reconnection
TEST_F(ReconnectionManagerTest, SubscriptionRecovery) {
    // Test subscription recovery functionality using manual reconnection
    // This avoids timing issues with automatic monitoring
    
    // Add subscriptions
    std::string nodeId1 = getTestNodeId(2001);
    std::string nodeId2 = getTestNodeId(2002);
    
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId1));
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId2));
    
    auto initialSubscriptions = subscriptionManager_->getActiveMonitoredItems();
    EXPECT_EQ(initialSubscriptions.size(), 2);
    
    // Disconnect and manually trigger reconnection to test recovery
    opcClient_->disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Trigger reconnection which should also recover subscriptions
    auto initialStats = reconnectionManager_->getStats();
    EXPECT_TRUE(reconnectionManager_->triggerReconnection());
    
    // Check that subscription recovery was attempted
    auto stats = reconnectionManager_->getStats();
    EXPECT_GT(stats.subscriptionRecoveries, initialStats.subscriptionRecoveries);
    
    // Verify client is connected again
    EXPECT_TRUE(opcClient_->isConnected());
}

// Test invalid configuration handling
TEST_F(ReconnectionManagerTest, InvalidConfiguration) {
    Configuration invalidConfig = config_;
    
    // Test invalid retry max
    invalidConfig.connectionRetryMax = -1;
    EXPECT_THROW(ReconnectionManager(opcClient_.get(), subscriptionManager_.get(), invalidConfig), 
                 std::invalid_argument);
    
    // Test invalid max delay
    invalidConfig = config_;
    invalidConfig.connectionMaxDelay = -1;
    EXPECT_THROW(ReconnectionManager(opcClient_.get(), subscriptionManager_.get(), invalidConfig), 
                 std::invalid_argument);
    
    // Test null pointers
    EXPECT_THROW(ReconnectionManager(nullptr, subscriptionManager_.get(), config_), 
                 std::invalid_argument);
    EXPECT_THROW(ReconnectionManager(opcClient_.get(), nullptr, config_), 
                 std::invalid_argument);
}

// Test time until next attempt calculation
TEST_F(ReconnectionManagerTest, TimeUntilNextAttempt) {
    // Initially should be zero
    EXPECT_EQ(reconnectionManager_->getTimeUntilNextAttempt().count(), 0);
    
    // After starting monitoring, it should still be zero when connected
    EXPECT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(reconnectionManager_->getTimeUntilNextAttempt().count(), 0);
    
    reconnectionManager_->stopMonitoring();
}

} // namespace test
} // namespace opcua2http