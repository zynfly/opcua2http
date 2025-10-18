#pragma once

#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "MockOPCUAServer.h"
#include "config/Configuration.h"
#include "opcua/OPCUAClient.h"
#include "cache/CacheManager.h"

namespace opcua2http {
namespace test {

/**
 * @brief Base class for OPC UA tests
 * 
 * Provides common setup and teardown for tests that need a mock OPC UA server
 * and basic client configuration. Uses a shared global mock server to avoid
 * port conflicts and improve test stability.
 */
class OPCUATestBase : public ::testing::Test {
protected:
    /**
     * @brief Constructor
     * @param useStandardVariables Whether to add standard test variables (default: true)
     */
    explicit OPCUATestBase(bool useStandardVariables = true);
    
    /**
     * @brief Virtual destructor
     */
    virtual ~OPCUATestBase() = default;
    
    /**
     * @brief Test setup - initializes shared mock server and configures client
     */
    void SetUp() override;
    
    /**
     * @brief Test teardown - cleans up client resources
     */
    void TearDown() override;
    
public:
    /**
     * @brief Initialize the shared mock server (called once)
     */
    static void initializeSharedMockServer();
    
    /**
     * @brief Shutdown the shared mock server (called once at end)
     */
    static void shutdownSharedMockServer();

protected:
    /**
     * @brief Get the shared mock server instance
     */
    static MockOPCUAServer* getSharedMockServer();
    
    /**
     * @brief Get a formatted node ID string for the test namespace
     * @param nodeId Numeric node ID
     */
    std::string getTestNodeId(UA_UInt32 nodeId) const;
    
    /**
     * @brief Create and initialize an OPC UA client
     * @return Initialized client (not connected)
     */
    std::unique_ptr<OPCUAClient> createOPCClient();
    
    /**
     * @brief Create and initialize a connected OPC UA client
     * @return Connected client, or nullptr if connection failed
     */
    std::unique_ptr<OPCUAClient> createConnectedOPCClient();
    
    /**
     * @brief Create a cache manager with default settings
     * @param expirationMinutes Cache expiration time (default: 60)
     * @param maxEntries Maximum cache entries (default: 1000)
     */
    std::unique_ptr<CacheManager> createCacheManager(int expirationMinutes = 60, size_t maxEntries = 1000);
    
    /**
     * @brief Wait for a condition with timeout
     * @param condition Function that returns true when condition is met
     * @param timeoutMs Timeout in milliseconds
     * @param checkIntervalMs Check interval in milliseconds
     * @return true if condition was met within timeout
     */
    bool waitForCondition(std::function<bool()> condition, int timeoutMs = 1000, int checkIntervalMs = 10);
    
    // Protected members accessible to derived classes
    MockOPCUAServer* mockServer_; // Pointer to shared server
    Configuration config_;
    
private:
    bool useStandardVariables_;
    static std::unique_ptr<MockOPCUAServer> sharedMockServer_;
    static std::mutex serverMutex_;
    static std::mutex setupMutex_;  // Mutex to serialize test SetUp calls
    static bool serverInitialized_;
};

/**
 * @brief Specialized test base for subscription tests
 * 
 * Includes additional setup for subscription-related testing with
 * shorter timeouts and specialized configuration.
 */
class SubscriptionTestBase : public OPCUATestBase {
protected:
    explicit SubscriptionTestBase();
    
    void SetUp() override;
    
    /**
     * @brief Update a test variable and wait for notification processing
     * @param nodeId Numeric node ID
     * @param newValue New value
     * @param client OPC client to run iterations on
     * @param maxIterations Maximum iterations to wait
     */
    void updateVariableAndWait(UA_UInt32 nodeId, const UA_Variant& newValue, 
                              OPCUAClient* client, int maxIterations = 20);
};

/**
 * @brief Test fixture for performance testing
 * 
 * Includes timing utilities and performance measurement helpers.
 */
class PerformanceTestBase : public OPCUATestBase {
protected:
    explicit PerformanceTestBase();
    
    /**
     * @brief Measure execution time of a function
     * @param func Function to measure
     * @return Execution time in milliseconds
     */
    template<typename Func>
    double measureExecutionTime(Func&& func) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        return duration.count() / 1000.0; // Return milliseconds
    }
    
    /**
     * @brief Add multiple test variables for performance testing
     * @param count Number of variables to add
     * @param startNodeId Starting node ID (default: 2000)
     */
    void addPerformanceTestVariables(size_t count, UA_UInt32 startNodeId = 2000);
};

} // namespace test
} // namespace opcua2http