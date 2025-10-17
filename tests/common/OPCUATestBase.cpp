#include "OPCUATestBase.h"
#include <chrono>
#include <thread>

namespace opcua2http {
namespace test {

uint16_t OPCUATestBase::nextPort_ = 4840;

OPCUATestBase::OPCUATestBase(uint16_t serverPort, bool useStandardVariables)
    : serverPort_(serverPort == 0 ? getNextAvailablePort() : serverPort)
    , useStandardVariables_(useStandardVariables) {
}

void OPCUATestBase::SetUp() {
    // Create mock server with unique namespace
    std::string namespaceName = "http://test.opcua.server.port" + std::to_string(serverPort_);
    mockServer_ = std::make_unique<MockOPCUAServer>(serverPort_, namespaceName);
    
    // Add standard test variables if requested
    if (useStandardVariables_) {
        mockServer_->addStandardTestVariables();
    }
    
    // Start server
    ASSERT_TRUE(mockServer_->start()) << "Failed to start mock OPC UA server on port " << serverPort_;
    
    // Configure test settings
    config_.opcEndpoint = mockServer_->getEndpoint();
    config_.securityMode = 1; // None
    config_.securityPolicy = "None";
    config_.defaultNamespace = mockServer_->getTestNamespaceIndex();
    config_.applicationUri = "urn:test:opcua:client:port" + std::to_string(serverPort_);
    config_.connectionRetryMax = 3;
    config_.connectionInitialDelay = 100;
    config_.connectionMaxRetry = 5;
    config_.connectionMaxDelay = 5000;
    config_.connectionRetryDelay = 1000;
}

void OPCUATestBase::TearDown() {
    if (mockServer_) {
        mockServer_->stop();
        mockServer_.reset();
    }
}

std::string OPCUATestBase::getTestNodeId(UA_UInt32 nodeId) const {
    return mockServer_->getNodeIdString(nodeId);
}

std::unique_ptr<OPCUAClient> OPCUATestBase::createOPCClient() {
    auto client = std::make_unique<OPCUAClient>();
    if (!client->initialize(config_)) {
        return nullptr;
    }
    return client;
}

std::unique_ptr<OPCUAClient> OPCUATestBase::createConnectedOPCClient() {
    auto client = createOPCClient();
    if (!client || !client->connect()) {
        return nullptr;
    }
    return client;
}

std::unique_ptr<CacheManager> OPCUATestBase::createCacheManager(int expirationMinutes, size_t maxEntries) {
    return std::make_unique<CacheManager>(expirationMinutes, maxEntries);
}

bool OPCUATestBase::waitForCondition(std::function<bool()> condition, int timeoutMs, int checkIntervalMs) {
    auto startTime = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(timeoutMs);
    
    while (true) {
        if (condition()) {
            return true;
        }
        
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed > timeout) {
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
    }
}

uint16_t OPCUATestBase::getNextAvailablePort() {
    return nextPort_++;
}

// SubscriptionTestBase implementation
SubscriptionTestBase::SubscriptionTestBase(uint16_t serverPort)
    : OPCUATestBase(serverPort, true) {
}

void SubscriptionTestBase::SetUp() {
    OPCUATestBase::SetUp();
    
    // Configure for faster subscription testing
    config_.connectionInitialDelay = 50;
    config_.connectionRetryDelay = 500;
}

void SubscriptionTestBase::updateVariableAndWait(UA_UInt32 nodeId, const UA_Variant& newValue, 
                                               OPCUAClient* client, int maxIterations) {
    // Update the server variable
    mockServer_->updateTestVariable(nodeId, newValue);
    
    // Allow time for notification processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Run client iterations to process notifications
    for (int i = 0; i < maxIterations; i++) {
        if (client) {
            client->runIterate(50);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// PerformanceTestBase implementation
PerformanceTestBase::PerformanceTestBase(uint16_t serverPort)
    : OPCUATestBase(serverPort, false) { // Don't add standard variables for performance tests
}

void PerformanceTestBase::addPerformanceTestVariables(size_t count, UA_UInt32 startNodeId) {
    for (size_t i = 0; i < count; i++) {
        UA_UInt32 nodeId = startNodeId + static_cast<UA_UInt32>(i);
        std::string name = "PerfVar" + std::to_string(i);
        
        // Alternate between different data types
        UA_Variant value;
        switch (i % 3) {
            case 0: {
                value = TestValueFactory::createInt32(static_cast<UA_Int32>(i));
                break;
            }
            case 1: {
                value = TestValueFactory::createString("Value" + std::to_string(i));
                break;
            }
            case 2: {
                value = TestValueFactory::createBoolean(i % 2 == 0);
                break;
            }
        }
        
        mockServer_->addTestVariable(nodeId, name, value);
        UA_Variant_clear(&value);
    }
}



} // namespace test
} // namespace opcua2http