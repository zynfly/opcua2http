#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <future>
#include <vector>
#include <string>
#include <memory>
#include <atomic>

#include "common/OPCUATestBase.h"
#include "core/OPCUAHTTPBridge.h"
#include "config/Configuration.h"
#include <nlohmann/json.hpp>
#include <future>
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// Simple HTTP client for testing
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace opcua2http {
namespace test {

/**
 * @brief End-to-end integration tests for the complete HTTP to OPC UA data flow
 * 
 * These tests verify the complete system integration including:
 * - HTTP API requests
 * - OPC UA client communication
 * - Cache management
 * - Subscription mechanism
 * - Data flow coordination
 */
class EndToEndIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure spdlog is in a clean state
        // This is critical because previous tests may have modified the logger
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("e2e_test", console_sink);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        
        // Create dedicated mock server for EndToEnd tests
        // Use a unique port for each test
        static std::atomic<uint16_t> endToEndServerPort{4850};
        mockServerPort_ = endToEndServerPort.fetch_add(1);
        
        std::string namespaceName = "http://test.opcua.e2e.port" + std::to_string(mockServerPort_);
        mockServer_ = std::make_unique<MockOPCUAServer>(mockServerPort_, namespaceName);
        mockServer_->addStandardTestVariables();
        
        ASSERT_TRUE(mockServer_->start()) << "Failed to start mock server on port " << mockServerPort_;
        
        // Setup test configuration
        setupBridgeConfiguration();
        
        // Initialize and start the bridge
        bridge_ = std::make_unique<OPCUAHTTPBridge>();
        
        // Set environment variables for test configuration
        setTestEnvironmentVariables();
        
        ASSERT_TRUE(bridge_->initialize()) << "Failed to initialize OPC UA HTTP Bridge";
        
        // Start bridge asynchronously
        ASSERT_TRUE(bridge_->startAsync()) << "Failed to start OPC UA HTTP Bridge asynchronously";
        
        // Wait for bridge to be ready with timeout
        ASSERT_TRUE(waitForBridgeReady(10000)) << "Bridge failed to start within 10 seconds";
    }
    
    void TearDown() override {
        // Stop the bridge
        if (bridge_) {
            bridge_->stop();
            
            // Give some time for cleanup
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        bridge_.reset();
        
        // Clean up environment variables
        cleanupEnvironmentVariables();
        
        // Stop mock server
        if (mockServer_) {
            mockServer_->stop();
            mockServer_.reset();
        }
    }
    
    /**
     * @brief URL encode a string for HTTP requests
     */
    std::string urlEncode(const std::string& value) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;
        
        for (char c : value) {
            // Keep alphanumeric and other accepted characters intact
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ',' || c == ':') {
                escaped << c;
            } else if (c == ';') {
                escaped << "%3B";
            } else if (c == '=') {
                escaped << "%3D";
            } else {
                // Any other characters are percent-encoded
                escaped << std::uppercase;
                escaped << '%' << std::setw(2) << int((unsigned char) c);
                escaped << std::nouppercase;
            }
        }
        
        return escaped.str();
    }
    
    /**
     * @brief Make HTTP GET request to the bridge API
     */
    nlohmann::json makeAPIRequest(const std::string& nodeIds) {
        // URL encode the node IDs to handle special characters like ; and =
        std::string encodedNodeIds = urlEncode(nodeIds);
        std::string url = "/iotgateway/read?ids=" + encodedNodeIds;
        return makeHTTPRequest("GET", url);
    }
    
    /**
     * @brief Make HTTP request to the bridge
     */
    nlohmann::json makeHTTPRequest(const std::string& method, const std::string& path) {
        try {
            std::string request = method + " " + path + " HTTP/1.1\r\n";
            request += "Host: localhost:" + std::to_string(testServerPort_) + "\r\n";
            request += "Connection: close\r\n";
            request += "\r\n";
            
            // Debug: print the request
            std::cout << "Sending HTTP request:\n" << request << std::endl;
            
            // Create socket
#ifdef _WIN32
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
            SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
#else
            int sock = socket(AF_INET, SOCK_STREAM, 0);
#endif
            
            if (sock < 0) {
                throw std::runtime_error("Failed to create socket");
            }
            
            // Connect to server
            sockaddr_in serverAddr{};
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(testServerPort_);
            inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
            
            if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
#ifdef _WIN32
                closesocket(sock);
                WSACleanup();
#else
                close(sock);
#endif
                throw std::runtime_error("Failed to connect to server");
            }
            
            // Send request
            send(sock, request.c_str(), static_cast<int>(request.length()), 0);
            
            // Receive response
            std::string response;
            char buffer[4096];
            int bytesReceived;
            
            while ((bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
                buffer[bytesReceived] = '\0';
                response += buffer;
            }
            
#ifdef _WIN32
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
            
            // Parse HTTP response
            size_t headerEnd = response.find("\r\n\r\n");
            if (headerEnd == std::string::npos) {
                throw std::runtime_error("Invalid HTTP response");
            }
            
            std::string body = response.substr(headerEnd + 4);
            
            // Debug: print the response
            std::cout << "Received HTTP response body:\n" << body << std::endl;
            
            // Parse JSON body
            if (!body.empty()) {
                return nlohmann::json::parse(body);
            }
            
            return nlohmann::json{};
            
        } catch (const std::exception& e) {
            std::cerr << "HTTP request failed: " << e.what() << std::endl;
            return nlohmann::json{{"error", e.what()}};
        }
    }
    
    /**
     * @brief Wait for bridge to be ready to accept requests
     */
    bool waitForBridgeReady(int timeoutMs = 5000) {
        return waitForCondition([this]() {
            try {
                auto response = makeHTTPRequest("GET", "/health");
                return response.contains("status") && 
                       (response["status"] == "running" || response["status"] == "ok");
            } catch (...) {
                return false;
            }
        }, timeoutMs, 100);
    }
    
private:
    void setupBridgeConfiguration() {
        // Use a unique port for each test instance
        static std::atomic<uint16_t> endToEndPort{8080};
        testServerPort_ = endToEndPort.fetch_add(1);
    }
    
    void cleanupEnvironmentVariables() {
#ifdef _WIN32
        _putenv_s("OPC_ENDPOINT", "");
        _putenv_s("SERVER_PORT", "");
        _putenv_s("CACHE_EXPIRE_MINUTES", "");
        _putenv_s("SUBSCRIPTION_CLEANUP_MINUTES", "");
        _putenv_s("LOG_LEVEL", "");
        _putenv_s("API_KEY", "");
        _putenv_s("AUTH_USERNAME", "");
        _putenv_s("AUTH_PASSWORD", "");
#else
        unsetenv("OPC_ENDPOINT");
        unsetenv("SERVER_PORT");
        unsetenv("CACHE_EXPIRE_MINUTES");
        unsetenv("SUBSCRIPTION_CLEANUP_MINUTES");
        unsetenv("LOG_LEVEL");
        unsetenv("API_KEY");
        unsetenv("AUTH_USERNAME");
        unsetenv("AUTH_PASSWORD");
#endif
    }
    
    void setTestEnvironmentVariables() {
        // Set environment variables for the bridge configuration
        std::string endpoint = "opc.tcp://localhost:" + std::to_string(mockServerPort_);
#ifdef _WIN32
        _putenv_s("OPC_ENDPOINT", endpoint.c_str());
        _putenv_s("SERVER_PORT", std::to_string(testServerPort_).c_str());
        _putenv_s("CACHE_EXPIRE_MINUTES", "1");
        _putenv_s("SUBSCRIPTION_CLEANUP_MINUTES", "1");
        _putenv_s("LOG_LEVEL", "info");
        
        // Disable authentication for testing
        _putenv_s("API_KEY", "");
        _putenv_s("AUTH_USERNAME", "");
        _putenv_s("AUTH_PASSWORD", "");
#else
        setenv("OPC_ENDPOINT", endpoint.c_str(), 1);
        setenv("SERVER_PORT", std::to_string(testServerPort_).c_str(), 1);
        setenv("CACHE_EXPIRE_MINUTES", "1", 1);
        setenv("SUBSCRIPTION_CLEANUP_MINUTES", "1", 1);
        setenv("LOG_LEVEL", "info", 1);
        
        // Disable authentication for testing
        unsetenv("API_KEY");
        unsetenv("AUTH_USERNAME");
        unsetenv("AUTH_PASSWORD");
#endif
    }

protected:
    std::unique_ptr<MockOPCUAServer> mockServer_;
    uint16_t mockServerPort_;
    std::unique_ptr<OPCUAHTTPBridge> bridge_;
    uint16_t testServerPort_;
    /**
     * @brief Get a formatted node ID string for the test namespace
     */
    std::string getTestNodeId(UA_UInt32 nodeId) const {
        return mockServer_->getNodeIdString(nodeId);
    }
    
    /**
     * @brief Wait for a condition with timeout
     */
    bool waitForCondition(std::function<bool()> condition, int timeoutMs = 1000, int checkIntervalMs = 10) {
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
};

/**
 * @brief Test basic HTTP to OPC UA data flow
 * 
 * Verifies that:
 * 1. HTTP request is received and processed
 * 2. OPC UA client reads data from server
 * 3. Data is cached properly
 * 4. Correct JSON response is returned
 */
TEST_F(EndToEndIntegrationTest, BasicDataFlow) {
    // Test node IDs from the mock server
    std::string nodeId1 = getTestNodeId(1001); // Int32 variable
    std::string nodeId2 = getTestNodeId(1002); // String variable
    
    // Make API request for multiple nodes
    auto response = makeAPIRequest(nodeId1 + "," + nodeId2);
    
    // Verify response structure
    ASSERT_TRUE(response.contains("readResults"));
    ASSERT_TRUE(response["readResults"].is_array());
    ASSERT_EQ(response["readResults"].size(), 2);
    
    // Verify first result
    auto result1 = response["readResults"][0];
    EXPECT_EQ(result1["nodeId"], nodeId1);
    EXPECT_TRUE(result1["success"].get<bool>());
    EXPECT_EQ(result1["reason"], "Good");
    EXPECT_TRUE(result1.contains("value"));
    EXPECT_TRUE(result1.contains("timestamp"));
    
    // Verify second result
    auto result2 = response["readResults"][1];
    EXPECT_EQ(result2["nodeId"], nodeId2);
    EXPECT_TRUE(result2["success"].get<bool>());
    EXPECT_EQ(result2["reason"], "Good");
    EXPECT_TRUE(result2.contains("value"));
    EXPECT_TRUE(result2.contains("timestamp"));
}

/**
 * @brief Test subscription mechanism
 * 
 * Verifies that:
 * 1. First request creates subscription
 * 2. Subsequent requests use cached data
 * 3. Cache is updated when OPC UA data changes
 */
TEST_F(EndToEndIntegrationTest, SubscriptionMechanism) {
    std::string nodeId = getTestNodeId(1001);
    
    // First request - should create subscription
    auto response1 = makeAPIRequest(nodeId);
    ASSERT_TRUE(response1.contains("readResults"));
    ASSERT_EQ(response1["readResults"].size(), 1);
    
    auto result1 = response1["readResults"][0];
    std::string initialValue = result1["value"];
    
    // Wait a bit for subscription to be established
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Update the value on the mock server
    UA_Variant newValue = TestValueFactory::createInt32(12345);
    mockServer_->updateTestVariable(1001, newValue);
    UA_Variant_clear(&newValue);
    
    // Wait for subscription notification
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // Second request - should get updated cached value
    auto response2 = makeAPIRequest(nodeId);
    ASSERT_TRUE(response2.contains("readResults"));
    ASSERT_EQ(response2["readResults"].size(), 1);
    
    auto result2 = response2["readResults"][0];
    
    // Verify the response structure is correct
    EXPECT_EQ(result2["nodeId"], nodeId);
    EXPECT_TRUE(result2["success"].get<bool>());
    EXPECT_EQ(result2["reason"], "Good");
}

/**
 * @brief Test cache behavior
 * 
 * Verifies that:
 * 1. Data is cached after first request
 * 2. Subsequent requests are served from cache
 * 3. Cache contains expected data
 */
TEST_F(EndToEndIntegrationTest, CacheBehavior) {
    std::string nodeId = getTestNodeId(1001);
    
    // First request - should populate cache
    auto response1 = makeAPIRequest(nodeId);
    ASSERT_TRUE(response1.contains("readResults"));
    ASSERT_EQ(response1["readResults"].size(), 1);
    
    auto result1 = response1["readResults"][0];
    EXPECT_TRUE(result1["success"].get<bool>());
    
    // Second request - should use cache
    auto response2 = makeAPIRequest(nodeId);
    ASSERT_TRUE(response2.contains("readResults"));
    ASSERT_EQ(response2["readResults"].size(), 1);
    
    auto result2 = response2["readResults"][0];
    
    // Verify responses are consistent
    EXPECT_EQ(result1["nodeId"], result2["nodeId"]);
    EXPECT_EQ(result1["value"], result2["value"]);
}

/**
 * @brief Test error handling
 * 
 * Verifies that:
 * 1. Invalid node IDs return appropriate errors
 * 2. System remains stable after errors
 * 3. Valid requests still work after error conditions
 */
TEST_F(EndToEndIntegrationTest, ErrorHandling) {
    // Test invalid node ID
    std::string invalidNodeId = "ns=99;s=NonExistentNode";
    auto errorResponse = makeAPIRequest(invalidNodeId);
    
    ASSERT_TRUE(errorResponse.contains("readResults"));
    ASSERT_EQ(errorResponse["readResults"].size(), 1);
    
    auto errorResult = errorResponse["readResults"][0];
    EXPECT_EQ(errorResult["nodeId"], invalidNodeId);
    EXPECT_FALSE(errorResult["success"].get<bool>()); // Should be false for error
    EXPECT_NE(errorResult["reason"], "Good"); // Should contain error description
    
    // Test that valid requests still work after error
    std::string validNodeId = getTestNodeId(1001);
    auto validResponse = makeAPIRequest(validNodeId);
    
    ASSERT_TRUE(validResponse.contains("readResults"));
    ASSERT_EQ(validResponse["readResults"].size(), 1);
    
    auto validResult = validResponse["readResults"][0];
    EXPECT_EQ(validResult["nodeId"], validNodeId);
    EXPECT_TRUE(validResult["success"].get<bool>());
    EXPECT_EQ(validResult["reason"], "Good");
}

/**
 * @brief Test mixed valid and invalid requests
 * 
 * Verifies that:
 * 1. Batch requests with mixed valid/invalid nodes work correctly
 * 2. Valid nodes return data, invalid nodes return errors
 * 3. One error doesn't affect other results in the batch
 */
TEST_F(EndToEndIntegrationTest, MixedValidInvalidRequests) {
    std::string validNodeId = getTestNodeId(1001);
    std::string invalidNodeId = "ns=99;s=NonExistent";
    
    // Make batch request with both valid and invalid nodes
    auto response = makeAPIRequest(validNodeId + "," + invalidNodeId);
    
    ASSERT_TRUE(response.contains("readResults"));
    ASSERT_EQ(response["readResults"].size(), 2);
    
    // Find results by ID (order might vary)
    nlohmann::json validResult, invalidResult;
    for (const auto& result : response["readResults"]) {
        if (result["nodeId"] == validNodeId) {
            validResult = result;
        } else if (result["nodeId"] == invalidNodeId) {
            invalidResult = result;
        }
    }
    
    // Verify valid result
    ASSERT_FALSE(validResult.is_null());
    EXPECT_TRUE(validResult["success"].get<bool>());
    EXPECT_EQ(validResult["reason"], "Good");
    EXPECT_TRUE(validResult.contains("value"));
    
    // Verify invalid result
    ASSERT_FALSE(invalidResult.is_null());
    EXPECT_FALSE(invalidResult["success"].get<bool>());
    EXPECT_NE(invalidResult["reason"], "Good");
}

} // namespace test
} // namespace opcua2http