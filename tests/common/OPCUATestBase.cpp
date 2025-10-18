#include "OPCUATestBase.h"
#include <chrono>
#include <thread>
#include <mutex>

namespace opcua2http {
namespace test {

// Static member initialization
std::unique_ptr<MockOPCUAServer> OPCUATestBase::sharedMockServer_ = nullptr;
std::mutex OPCUATestBase::serverMutex_;
std::mutex OPCUATestBase::setupMutex_;
bool OPCUATestBase::serverInitialized_ = false;

OPCUATestBase::OPCUATestBase(bool useStandardVariables)
    : mockServer_(nullptr)
    , useStandardVariables_(useStandardVariables) {
}

void OPCUATestBase::initializeSharedMockServer() {
    std::lock_guard<std::mutex> lock(serverMutex_);
    
    if (!serverInitialized_) {
        // Create shared mock server on fixed port
        constexpr uint16_t SHARED_SERVER_PORT = 4840;
        std::string namespaceName = "http://test.opcua.shared.server";
        
        sharedMockServer_ = std::make_unique<MockOPCUAServer>(SHARED_SERVER_PORT, namespaceName);
        
        // Disable verbose logging to reduce noise
        sharedMockServer_->setVerboseLogging(false);
        
        // Add standard test variables
        sharedMockServer_->addStandardTestVariables();
        
        // Start server
        if (!sharedMockServer_->start()) {
            throw std::runtime_error("Failed to start shared mock OPC UA server");
        }
        
        serverInitialized_ = true;
    }
}

void OPCUATestBase::shutdownSharedMockServer() {
    std::lock_guard<std::mutex> lock(serverMutex_);
    
    if (sharedMockServer_) {
        sharedMockServer_->stop();
        sharedMockServer_.reset();
        serverInitialized_ = false;
    }
}

MockOPCUAServer* OPCUATestBase::getSharedMockServer() {
    std::lock_guard<std::mutex> lock(serverMutex_);
    return sharedMockServer_.get();
}

void OPCUATestBase::SetUp() {
    // Serialize SetUp calls to prevent concurrent access to shared MockServer
    std::lock_guard<std::mutex> setupLock(setupMutex_);
    
    // Initialize shared server if not already done
    initializeSharedMockServer();
    
    // Get reference to shared server
    mockServer_ = getSharedMockServer();
    ASSERT_NE(mockServer_, nullptr) << "Shared mock server not initialized";
    
    // Reset standard test variables to known state if requested
    if (useStandardVariables_) {
        UA_Variant intValue = TestValueFactory::createInt32(42);
        mockServer_->updateTestVariable(1001, intValue);
        UA_Variant_clear(&intValue);
        
        UA_Variant stringValue = TestValueFactory::createString("Hello World");
        mockServer_->updateTestVariable(1002, stringValue);
        UA_Variant_clear(&stringValue);
        
        UA_Variant boolValue = TestValueFactory::createBoolean(true);
        mockServer_->updateTestVariable(1003, boolValue);
        UA_Variant_clear(&boolValue);
    }
    
    // Configure test settings
    config_.opcEndpoint = mockServer_->getEndpoint();
    config_.securityMode = 1; // None
    config_.securityPolicy = "None";
    config_.defaultNamespace = mockServer_->getTestNamespaceIndex();
    config_.applicationUri = "urn:test:opcua:client:shared";
    config_.connectionRetryMax = 3;
    config_.connectionInitialDelay = 100;
    config_.connectionMaxRetry = 5;
    config_.connectionMaxDelay = 5000;
    config_.connectionRetryDelay = 1000;
}

void OPCUATestBase::TearDown() {
    // Serialize TearDown to ensure clean disconnection before next test
    std::lock_guard<std::mutex> setupLock(setupMutex_);
    
    // Don't stop the shared server, just clean up test-specific resources
    mockServer_ = nullptr;
    
    // CRITICAL: Wait longer to ensure all OPC UA connections are fully closed
    // The MockServer needs time to process disconnections and clean up sessions
    // Without this delay, the next test may encounter corrupted server state
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
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

// SubscriptionTestBase implementation
SubscriptionTestBase::SubscriptionTestBase()
    : OPCUATestBase(true) {
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
PerformanceTestBase::PerformanceTestBase()
    : OPCUATestBase(false) { // Don't add standard variables for performance tests
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