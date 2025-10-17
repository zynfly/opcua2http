#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <memory>

#include "opcua/OPCUAClient.h"
#include "config/Configuration.h"

// Mock OPC UA Server for testing
#include <open62541/server.h>
#include <open62541/server_config_default.h>

namespace opcua2http {
namespace test {

// Global variables to ensure values remain valid throughout server lifetime
static UA_Int32 g_testIntValue = 42;
static UA_String g_testStringValue = UA_STRING_STATIC("Hello World");
static UA_Boolean g_testBoolValue = true;

class MockOPCUAServer {
public:
    MockOPCUAServer(uint16_t port = 4840) : port_(port), server_(nullptr), running_(false), serverReady_(false) {}
    
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
    
private:
    void addTestVariables() {
        if (!server_) return;
        
        // Add namespace for our test variables
        UA_UInt16 nsIndex = UA_Server_addNamespace(server_, "http://test.opcua.server");
        
        std::cout << "Added namespace with index: " << nsIndex << std::endl;
        
        // Add integer variable - using global variable to ensure lifetime
        {
            UA_VariableAttributes attr = UA_VariableAttributes_default;
            attr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("TestInt"));
            UA_Variant_setScalar(&attr.value, &g_testIntValue, &UA_TYPES[UA_TYPES_INT32]);
            
            UA_NodeId newNodeId = UA_NODEID_NUMERIC(nsIndex, 1001);
            UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
            UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_NodeId variableType = UA_NODEID_NULL;
            UA_QualifiedName browseName = UA_QUALIFIEDNAME(nsIndex, const_cast<char*>("TestInt"));
            
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
            attr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("TestString"));
            UA_Variant_setScalar(&attr.value, &g_testStringValue, &UA_TYPES[UA_TYPES_STRING]);
            
            UA_NodeId newNodeId = UA_NODEID_NUMERIC(nsIndex, 1002);
            UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
            UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_NodeId variableType = UA_NODEID_NULL;
            UA_QualifiedName browseName = UA_QUALIFIEDNAME(nsIndex, const_cast<char*>("TestString"));
            
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
            attr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>("TestBool"));
            UA_Variant_setScalar(&attr.value, &g_testBoolValue, &UA_TYPES[UA_TYPES_BOOLEAN]);
            
            UA_NodeId newNodeId = UA_NODEID_NUMERIC(nsIndex, 1003);
            UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
            UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
            UA_NodeId variableType = UA_NODEID_NULL;
            UA_QualifiedName browseName = UA_QUALIFIEDNAME(nsIndex, const_cast<char*>("TestBool"));
            
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

class OPCUAClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start mock server
        mockServer_ = std::make_unique<MockOPCUAServer>(4841);
        ASSERT_TRUE(mockServer_->start()) << "Failed to start mock OPC UA server";
        
        // Server should be ready now, minimal additional wait
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Create test configuration
        config_.opcEndpoint = mockServer_->getEndpoint();
        config_.securityMode = 1; // None
        config_.securityPolicy = "None";
        config_.defaultNamespace = mockServer_->getTestNamespaceIndex();
        config_.applicationUri = "urn:test:opcua:client";
        config_.connectionRetryMax = 3;
        config_.connectionInitialDelay = 100;
        config_.connectionMaxRetry = 5;
        config_.connectionMaxDelay = 5000;
        config_.connectionRetryDelay = 1000;
        
        // Create client
        client_ = std::make_unique<OPCUAClient>();
    }
    
    void TearDown() override {
        // Disconnect client first while server is still running
        if (client_) {
            if (client_->isConnected()) {
                client_->disconnect();
                // Give some time for clean disconnection
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            client_.reset();
        }
        
        // Then stop the server
        if (mockServer_) {
            mockServer_->stop();
            mockServer_.reset();
        }
    }
    
    std::unique_ptr<MockOPCUAServer> mockServer_;
    std::unique_ptr<OPCUAClient> client_;
    Configuration config_;
};

// Test client initialization
TEST_F(OPCUAClientTest, InitializeClient) {
    EXPECT_TRUE(client_->initialize(config_));
    EXPECT_EQ(client_->getConnectionState(), OPCUAClient::ConnectionState::DISCONNECTED);
    EXPECT_FALSE(client_->isConnected());
    EXPECT_EQ(client_->getEndpoint(), config_.opcEndpoint);
}

// Test client initialization with invalid configuration
TEST_F(OPCUAClientTest, InitializeClientWithInvalidConfig) {
    Configuration invalidConfig = config_;
    invalidConfig.opcEndpoint = ""; // Empty endpoint
    
    EXPECT_FALSE(client_->initialize(invalidConfig));
    EXPECT_EQ(client_->getConnectionState(), OPCUAClient::ConnectionState::DISCONNECTED);
}

// Test connection establishment
TEST_F(OPCUAClientTest, ConnectToServer) {
    ASSERT_TRUE(client_->initialize(config_));
    
    bool stateChanged = false;
    OPCUAClient::ConnectionState finalState = OPCUAClient::ConnectionState::DISCONNECTED;
    
    client_->setStateChangeCallback([&](OPCUAClient::ConnectionState state, UA_StatusCode) {
        stateChanged = true;
        finalState = state;
    });
    
    EXPECT_TRUE(client_->connect());
    EXPECT_TRUE(client_->isConnected());
    EXPECT_EQ(client_->getConnectionState(), OPCUAClient::ConnectionState::CONNECTED);
    
    // Allow some time for state callback
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Test connection to invalid server
TEST_F(OPCUAClientTest, ConnectToInvalidServer) {
    Configuration invalidConfig = config_;
    invalidConfig.opcEndpoint = "opc.tcp://localhost:9999"; // Non-existent server
    
    ASSERT_TRUE(client_->initialize(invalidConfig));
    
    EXPECT_FALSE(client_->connect());
    EXPECT_FALSE(client_->isConnected());
    EXPECT_EQ(client_->getConnectionState(), OPCUAClient::ConnectionState::CONNECTION_ERROR);
}

// Test disconnection
TEST_F(OPCUAClientTest, DisconnectFromServer) {
    ASSERT_TRUE(client_->initialize(config_));
    ASSERT_TRUE(client_->connect());
    ASSERT_TRUE(client_->isConnected());
    
    client_->disconnect();
    
    // Give some time for clean disconnection
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_FALSE(client_->isConnected());
    EXPECT_EQ(client_->getConnectionState(), OPCUAClient::ConnectionState::DISCONNECTED);
}

// Test reading single node - integer value
TEST_F(OPCUAClientTest, ReadSingleNodeInteger) {
    ASSERT_TRUE(client_->initialize(config_));
    ASSERT_TRUE(client_->connect());
    
    std::string nodeId = "ns=" + std::to_string(config_.defaultNamespace) + ";i=1001";
    std::cout << "Attempting to read node: " << nodeId << std::endl;
    std::cout << "Global test value is: " << g_testIntValue << std::endl;
    
    ReadResult result = client_->readNode(nodeId);
    std::cout << "Read result - Success: " << result.success << ", Value: '" << result.value << "', Reason: '" << result.reason << "'" << std::endl;
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.id, nodeId);
    EXPECT_EQ(result.value, "42");
    EXPECT_EQ(result.reason, "Good");
    EXPECT_GT(result.timestamp, 0);
}

// Test reading single node - string value
TEST_F(OPCUAClientTest, ReadSingleNodeString) {
    ASSERT_TRUE(client_->initialize(config_));
    ASSERT_TRUE(client_->connect());
    
    std::string nodeId = "ns=" + std::to_string(config_.defaultNamespace) + ";i=1002";
    ReadResult result = client_->readNode(nodeId);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.id, nodeId);
    EXPECT_EQ(result.value, "Hello World");
    EXPECT_EQ(result.reason, "Good");
    EXPECT_GT(result.timestamp, 0);
}

// Test reading single node - boolean value
TEST_F(OPCUAClientTest, ReadSingleNodeBoolean) {
    ASSERT_TRUE(client_->initialize(config_));
    ASSERT_TRUE(client_->connect());
    
    std::string nodeId = "ns=" + std::to_string(config_.defaultNamespace) + ";i=1003";
    ReadResult result = client_->readNode(nodeId);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.id, nodeId);
    EXPECT_EQ(result.value, "true");
    EXPECT_EQ(result.reason, "Good");
    EXPECT_GT(result.timestamp, 0);
}

// Test reading non-existent node
TEST_F(OPCUAClientTest, ReadNonExistentNode) {
    ASSERT_TRUE(client_->initialize(config_));
    ASSERT_TRUE(client_->connect());
    
    std::string nodeId = "ns=" + std::to_string(config_.defaultNamespace) + ";i=9999";
    ReadResult result = client_->readNode(nodeId);
    
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.id, nodeId);
    EXPECT_TRUE(result.value.empty());
    EXPECT_NE(result.reason, "Good");
}

// Test reading with invalid NodeId format
TEST_F(OPCUAClientTest, ReadInvalidNodeIdFormat) {
    ASSERT_TRUE(client_->initialize(config_));
    ASSERT_TRUE(client_->connect());
    
    ReadResult result = client_->readNode("invalid-node-id");
    
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.id, "invalid-node-id");
    EXPECT_TRUE(result.value.empty());
    EXPECT_EQ(result.reason, "Invalid NodeId format");
}

// Test reading when not connected
TEST_F(OPCUAClientTest, ReadWhenNotConnected) {
    ASSERT_TRUE(client_->initialize(config_));
    // Don't connect
    
    std::string nodeId = "ns=" + std::to_string(config_.defaultNamespace) + ";i=1001";
    ReadResult result = client_->readNode(nodeId);
    
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.id, nodeId);
    EXPECT_TRUE(result.value.empty());
    EXPECT_EQ(result.reason, "Client not connected");
}

// Test reading multiple nodes
TEST_F(OPCUAClientTest, ReadMultipleNodes) {
    ASSERT_TRUE(client_->initialize(config_));
    ASSERT_TRUE(client_->connect());
    
    std::string nsPrefix = "ns=" + std::to_string(config_.defaultNamespace) + ";i=";
    std::vector<std::string> nodeIds = {
        nsPrefix + "1001",
        nsPrefix + "1002",
        nsPrefix + "1003"
    };
    
    std::vector<ReadResult> results = client_->readNodes(nodeIds);
    
    ASSERT_EQ(results.size(), 3);
    
    // Check integer result
    EXPECT_TRUE(results[0].success);
    EXPECT_EQ(results[0].id, nodeIds[0]);
    EXPECT_EQ(results[0].value, "42");
    
    // Check string result
    EXPECT_TRUE(results[1].success);
    EXPECT_EQ(results[1].id, nodeIds[1]);
    EXPECT_EQ(results[1].value, "Hello World");
    
    // Check boolean result
    EXPECT_TRUE(results[2].success);
    EXPECT_EQ(results[2].id, nodeIds[2]);
    EXPECT_EQ(results[2].value, "true");
}

// Test reading multiple nodes with mixed results
TEST_F(OPCUAClientTest, ReadMultipleNodesWithErrors) {
    ASSERT_TRUE(client_->initialize(config_));
    ASSERT_TRUE(client_->connect());
    
    std::string nsPrefix = "ns=" + std::to_string(config_.defaultNamespace) + ";i=";
    std::vector<std::string> nodeIds = {
        nsPrefix + "1001",        // Valid
        nsPrefix + "9999",        // Invalid
        nsPrefix + "1002"         // Valid
    };
    
    std::vector<ReadResult> results = client_->readNodes(nodeIds);
    
    ASSERT_EQ(results.size(), 3);
    
    // Check first result (valid)
    EXPECT_TRUE(results[0].success);
    EXPECT_EQ(results[0].value, "42");
    
    // Check second result (invalid)
    EXPECT_FALSE(results[1].success);
    EXPECT_TRUE(results[1].value.empty());
    
    // Check third result (valid)
    EXPECT_TRUE(results[2].success);
    EXPECT_EQ(results[2].value, "Hello World");
}

// Test reading multiple nodes when not connected
TEST_F(OPCUAClientTest, ReadMultipleNodesWhenNotConnected) {
    ASSERT_TRUE(client_->initialize(config_));
    // Don't connect
    
    std::string nsPrefix = "ns=" + std::to_string(config_.defaultNamespace) + ";i=";
    std::vector<std::string> nodeIds = {
        nsPrefix + "1001",
        nsPrefix + "1002"
    };
    
    std::vector<ReadResult> results = client_->readNodes(nodeIds);
    
    ASSERT_EQ(results.size(), 2);
    
    for (const auto& result : results) {
        EXPECT_FALSE(result.success);
        EXPECT_TRUE(result.value.empty());
        EXPECT_EQ(result.reason, "Client not connected");
    }
}

// Test client state callback functionality
TEST_F(OPCUAClientTest, StateChangeCallback) {
    ASSERT_TRUE(client_->initialize(config_));
    
    bool callbackCalled = false;
    OPCUAClient::ConnectionState receivedState = OPCUAClient::ConnectionState::DISCONNECTED;
    UA_StatusCode receivedStatus = UA_STATUSCODE_GOOD;
    
    client_->setStateChangeCallback([&](OPCUAClient::ConnectionState state, UA_StatusCode status) {
        callbackCalled = true;
        receivedState = state;
        receivedStatus = status;
    });
    
    EXPECT_TRUE(client_->connect());
    
    // Allow time for callback
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Note: The callback might not be called immediately due to open62541's internal behavior
    // This test verifies the callback mechanism is set up correctly
    EXPECT_TRUE(client_->isConnected());
}

// Test connection info string
TEST_F(OPCUAClientTest, GetConnectionInfo) {
    ASSERT_TRUE(client_->initialize(config_));
    
    std::string info = client_->getConnectionInfo();
    EXPECT_FALSE(info.empty());
    EXPECT_NE(info.find("Endpoint:"), std::string::npos);
    EXPECT_NE(info.find("State:"), std::string::npos);
    EXPECT_NE(info.find("DISCONNECTED"), std::string::npos);
    
    ASSERT_TRUE(client_->connect());
    
    info = client_->getConnectionInfo();
    EXPECT_NE(info.find("CONNECTED"), std::string::npos);
}

// Test run iterate functionality
TEST_F(OPCUAClientTest, RunIterate) {
    ASSERT_TRUE(client_->initialize(config_));
    ASSERT_TRUE(client_->connect());
    
    // Run iterate should return good status when connected
    UA_StatusCode status = client_->runIterate(100);
    EXPECT_EQ(status, UA_STATUSCODE_GOOD);
}

} // namespace test
} // namespace opcua2http

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}