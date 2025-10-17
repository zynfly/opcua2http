#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <functional>
#include <iostream>

#include <open62541/server.h>
#include <open62541/server_config_default.h>

namespace opcua2http {
namespace test {

/**
 * @brief Test variable configuration for mock server
 */
struct TestVariable {
    UA_UInt32 nodeId;
    std::string name;
    UA_Variant value;
    
    TestVariable(UA_UInt32 id, const std::string& varName, const UA_Variant& val)
        : nodeId(id), name(varName) {
        UA_Variant_copy(&val, &value);
    }
    
    ~TestVariable() {
        UA_Variant_clear(&value);
    }
    
    // Move constructor
    TestVariable(TestVariable&& other) noexcept
        : nodeId(other.nodeId), name(std::move(other.name)) {
        value = other.value;
        UA_Variant_init(&other.value);
    }
    
    // Move assignment
    TestVariable& operator=(TestVariable&& other) noexcept {
        if (this != &other) {
            UA_Variant_clear(&value);
            nodeId = other.nodeId;
            name = std::move(other.name);
            value = other.value;
            UA_Variant_init(&other.value);
        }
        return *this;
    }
    
    // Delete copy operations to avoid double-free
    TestVariable(const TestVariable&) = delete;
    TestVariable& operator=(const TestVariable&) = delete;
};

/**
 * @brief Reusable mock OPC UA server for testing
 * 
 * This class provides a configurable mock OPC UA server that can be used
 * across different test suites to reduce code duplication.
 */
class MockOPCUAServer {
public:
    /**
     * @brief Constructor
     * @param port Server port (default: 4840)
     * @param namespaceName Custom namespace name (default: "http://test.opcua.server")
     */
    explicit MockOPCUAServer(uint16_t port = 4840, 
                            const std::string& namespaceName = "http://test.opcua.server");
    
    /**
     * @brief Destructor - automatically stops server
     */
    ~MockOPCUAServer();
    
    /**
     * @brief Start the mock server
     * @return true if started successfully
     */
    bool start();
    
    /**
     * @brief Stop the mock server
     */
    void stop();
    
    /**
     * @brief Check if server is running
     */
    bool isRunning() const { return running_ && serverReady_; }
    
    /**
     * @brief Get server endpoint URL
     */
    std::string getEndpoint() const;
    
    /**
     * @brief Get test namespace index
     */
    UA_UInt16 getTestNamespaceIndex() const { return testNamespaceIndex_; }
    
    /**
     * @brief Add a test variable to the server
     * @param nodeId Numeric node ID
     * @param name Variable name
     * @param value Initial value
     * @return true if added successfully
     */
    bool addTestVariable(UA_UInt32 nodeId, const std::string& name, const UA_Variant& value);
    
    /**
     * @brief Add standard test variables (Int32, String, Boolean)
     * Creates variables with node IDs 1001, 1002, 1003
     */
    void addStandardTestVariables();
    
    /**
     * @brief Update a test variable value
     * @param nodeId Numeric node ID
     * @param newValue New value
     */
    void updateTestVariable(UA_UInt32 nodeId, const UA_Variant& newValue);
    
    /**
     * @brief Get formatted node ID string
     * @param nodeId Numeric node ID
     */
    std::string getNodeIdString(UA_UInt32 nodeId) const;
    
    /**
     * @brief Set startup timeout in milliseconds (default: 1000ms)
     */
    void setStartupTimeout(int timeoutMs) { startupTimeoutMs_ = timeoutMs; }
    
    /**
     * @brief Enable/disable verbose logging
     */
    void setVerboseLogging(bool enabled) { verboseLogging_ = enabled; }

private:
    void serverThreadFunction();
    bool waitForServerReady();
    void logMessage(const std::string& message) const;
    bool addTestVariableInternal(UA_UInt32 nodeId, const std::string& name, const UA_Variant& value);
    
    uint16_t port_;
    std::string namespaceName_;
    UA_Server* server_;
    std::atomic<bool> running_;
    std::atomic<bool> serverReady_;
    std::thread serverThread_;
    UA_UInt16 testNamespaceIndex_;
    int startupTimeoutMs_;
    bool verboseLogging_;
    
    std::vector<TestVariable> testVariables_;
};

/**
 * @brief Helper class for creating common test values
 */
class TestValueFactory {
public:
    static UA_Variant createInt32(UA_Int32 value);
    static UA_Variant createString(const std::string& value);
    static UA_Variant createBoolean(UA_Boolean value);
    static UA_Variant createDouble(UA_Double value);
    static UA_Variant createFloat(UA_Float value);
};

} // namespace test
} // namespace opcua2http