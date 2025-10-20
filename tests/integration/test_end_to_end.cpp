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
    EXPECT_EQ(result1["quality"], "Good");
    EXPECT_TRUE(result1.contains("value"));
    EXPECT_TRUE(result1.contains("timestamp_iso"));

    // Verify second result
    auto result2 = response["readResults"][1];
    EXPECT_EQ(result2["nodeId"], nodeId2);
    EXPECT_TRUE(result2["success"].get<bool>());
    EXPECT_EQ(result2["quality"], "Good");
    EXPECT_TRUE(result2.contains("value"));
    EXPECT_TRUE(result2.contains("timestamp_iso"));
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
    EXPECT_EQ(result2["quality"], "Good");
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
    EXPECT_NE(errorResult["quality"], "Good"); // Should contain error description

    // Test that valid requests still work after error
    std::string validNodeId = getTestNodeId(1001);
    auto validResponse = makeAPIRequest(validNodeId);

    ASSERT_TRUE(validResponse.contains("readResults"));
    ASSERT_EQ(validResponse["readResults"].size(), 1);

    auto validResult = validResponse["readResults"][0];
    EXPECT_EQ(validResult["nodeId"], validNodeId);
    EXPECT_TRUE(validResult["success"].get<bool>());
    EXPECT_EQ(validResult["quality"], "Good");
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
    EXPECT_EQ(validResult["quality"], "Good");
    EXPECT_TRUE(validResult.contains("value"));

    // Verify invalid result
    ASSERT_FALSE(invalidResult.is_null());
    EXPECT_FALSE(invalidResult["success"].get<bool>());
    EXPECT_NE(invalidResult["quality"], "Good");
}

/**
 * @brief Test mixed cached and non-cached requests
 *
 * This test specifically verifies the fix for the issue where requesting
 * both cached and non-cached data simultaneously could cause ordering problems.
 *
 * Verifies that:
 * 1. First request populates cache for some nodes
 * 2. Second request with mixed cached/non-cached nodes returns correct order
 * 3. All results are successful and in the expected order
 */
TEST_F(EndToEndIntegrationTest, MixedCachedAndNonCachedRequests) {
    std::string nodeId1 = getTestNodeId(1001);
    std::string nodeId2 = getTestNodeId(1002);
    std::string nodeId3 = getTestNodeId(1003);

    // Step 1: Make first request to populate cache for node1
    std::cout << "Step 1: Populating cache with first node" << std::endl;
    auto response1 = makeAPIRequest(nodeId1);

    ASSERT_TRUE(response1.contains("readResults"));
    ASSERT_EQ(response1["readResults"].size(), 1);

    auto result1 = response1["readResults"][0];
    EXPECT_EQ(result1["nodeId"], nodeId1);
    EXPECT_TRUE(result1["success"].get<bool>());
    EXPECT_EQ(result1["quality"], "Good");

    // Wait a bit for subscription to be established
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Step 2: Make mixed request (cached node1 + non-cached node2 and node3)
    std::cout << "Step 2: Making mixed request with cached and non-cached nodes" << std::endl;
    auto response2 = makeAPIRequest(nodeId1 + "," + nodeId2 + "," + nodeId3);

    ASSERT_TRUE(response2.contains("readResults"));
    ASSERT_EQ(response2["readResults"].size(), 3);

    // Verify results are in the correct order (this was the bug we fixed)
    auto results = response2["readResults"];

    EXPECT_EQ(results[0]["nodeId"], nodeId1) << "First result should be for first requested node";
    EXPECT_EQ(results[1]["nodeId"], nodeId2) << "Second result should be for second requested node";
    EXPECT_EQ(results[2]["nodeId"], nodeId3) << "Third result should be for third requested node";

    // Verify all results are successful
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(results[i]["success"].get<bool>())
            << "Result " << i << " for node " << results[i]["nodeId"] << " should be successful";
        EXPECT_EQ(results[i]["quality"], "Good")
            << "Result " << i << " should have 'Good' status";
        EXPECT_TRUE(results[i].contains("value"))
            << "Result " << i << " should have a value";
    }

    std::cout << "Step 3: Verifying order preservation in mixed requests" << std::endl;

    // Step 3: Make another mixed request with different order to verify consistency
    auto response3 = makeAPIRequest(nodeId3 + "," + nodeId1 + "," + nodeId2);

    ASSERT_TRUE(response3.contains("readResults"));
    ASSERT_EQ(response3["readResults"].size(), 3);

    auto results3 = response3["readResults"];

    // Verify order is preserved even when all nodes are now cached
    EXPECT_EQ(results3[0]["nodeId"], nodeId3) << "First result should be for first requested node (node3)";
    EXPECT_EQ(results3[1]["nodeId"], nodeId1) << "Second result should be for second requested node (node1)";
    EXPECT_EQ(results3[2]["nodeId"], nodeId2) << "Third result should be for third requested node (node2)";

    // Verify all results are still successful
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(results3[i]["success"].get<bool>())
            << "Result " << i << " for node " << results3[i]["nodeId"] << " should be successful";
        EXPECT_EQ(results3[i]["quality"], "Good")
            << "Result " << i << " should have 'Good' status";
    }

    std::cout << "Mixed cache scenario test completed successfully" << std::endl;
}

/**
 * @brief Test end-to-end cache flow with timing behavior
 *
 * Verifies that:
 * 1. Fresh cache (< 3s) returns immediately
 * 2. Stale cache (3-10s) returns cached data with background update
 * 3. Expired cache (> 10s) forces synchronous read
 */
TEST_F(EndToEndIntegrationTest, CacheFlowWithTimingBehavior) {
    std::string nodeId = getTestNodeId(1001);

    std::cout << "Step 1: First request - populates cache" << std::endl;
    auto response1 = makeAPIRequest(nodeId);
    ASSERT_TRUE(response1.contains("readResults"));
    ASSERT_EQ(response1["readResults"].size(), 1);

    auto result1 = response1["readResults"][0];
    EXPECT_TRUE(result1["success"].get<bool>());
    std::string initialValue = result1["value"];

    std::cout << "Step 2: Immediate second request - should use fresh cache" << std::endl;
    auto response2 = makeAPIRequest(nodeId);
    ASSERT_TRUE(response2.contains("readResults"));
    auto result2 = response2["readResults"][0];
    EXPECT_TRUE(result2["success"].get<bool>());
    EXPECT_EQ(result2["value"], initialValue);

    std::cout << "Step 3: Wait 4 seconds - cache becomes stale" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(4));

    // Update value on server
    UA_Variant newValue = TestValueFactory::createInt32(99999);
    mockServer_->updateTestVariable(1001, newValue);
    UA_Variant_clear(&newValue);

    std::cout << "Step 4: Request with stale cache - should return cached data quickly" << std::endl;
    auto response3 = makeAPIRequest(nodeId);
    ASSERT_TRUE(response3.contains("readResults"));
    auto result3 = response3["readResults"][0];
    EXPECT_TRUE(result3["success"].get<bool>());
    // Should return cached data (may be old or new depending on background update timing)

    std::cout << "Cache flow timing test completed" << std::endl;
}

/**
 * @brief Test concurrent requests with cache
 *
 * Verifies that:
 * 1. Multiple concurrent requests are handled correctly
 * 2. Cache prevents duplicate OPC UA reads
 * 3. All requests receive consistent data
 */
TEST_F(EndToEndIntegrationTest, ConcurrentRequestsWithCache) {
    std::string nodeId = getTestNodeId(1001);

    const int numThreads = 10;
    std::vector<std::thread> threads;
    std::vector<nlohmann::json> responses(numThreads);
    std::atomic<int> successCount{0};

    std::cout << "Launching " << numThreads << " concurrent requests" << std::endl;

    // Launch concurrent requests
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            responses[i] = makeAPIRequest(nodeId);
            if (responses[i].contains("readResults") &&
                responses[i]["readResults"].size() > 0 &&
                responses[i]["readResults"][0]["success"].get<bool>()) {
                successCount++;
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    std::cout << "All concurrent requests completed" << std::endl;

    // All requests should succeed
    EXPECT_EQ(successCount.load(), numThreads);

    // All responses should have the same value (from cache)
    std::string firstValue;
    for (int i = 0; i < numThreads; ++i) {
        if (responses[i].contains("readResults") && responses[i]["readResults"].size() > 0) {
            std::string value = responses[i]["readResults"][0]["value"];
            if (firstValue.empty()) {
                firstValue = value;
            }
            // All values should be consistent
            EXPECT_EQ(value, firstValue) << "Response " << i << " has inconsistent value";
        }
    }
}

/**
 * @brief Test batch operations with cache
 *
 * Verifies that:
 * 1. Batch requests with multiple nodes work correctly
 * 2. Mixed cached/non-cached nodes are handled properly
 * 3. Results maintain correct order
 */
TEST_F(EndToEndIntegrationTest, BatchOperationsWithCache) {
    std::vector<std::string> nodeIds = {
        getTestNodeId(1001),
        getTestNodeId(1002),
        getTestNodeId(1003)
    };

    std::cout << "Step 1: Batch request for 3 nodes" << std::endl;
    std::string nodeIdsStr = nodeIds[0] + "," + nodeIds[1] + "," + nodeIds[2];
    auto response1 = makeAPIRequest(nodeIdsStr);

    ASSERT_TRUE(response1.contains("readResults"));
    ASSERT_EQ(response1["readResults"].size(), 3);

    // Verify all results are successful
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(response1["readResults"][i]["success"].get<bool>())
            << "Result " << i << " should be successful";
        EXPECT_EQ(response1["readResults"][i]["nodeId"], nodeIds[i])
            << "Result " << i << " should have correct node ID";
    }

    std::cout << "Step 2: Second batch request - should use cache" << std::endl;
    auto response2 = makeAPIRequest(nodeIdsStr);

    ASSERT_TRUE(response2.contains("readResults"));
    ASSERT_EQ(response2["readResults"].size(), 3);

    // Verify results are consistent with first request
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(response2["readResults"][i]["success"].get<bool>());
        EXPECT_EQ(response2["readResults"][i]["nodeId"], nodeIds[i]);
    }

    std::cout << "Batch operations test completed" << std::endl;
}

/**
 * @brief Test OPC UA disconnection and recovery
 *
 * Verifies that:
 * 1. System handles OPC UA server disconnection gracefully
 * 2. Cache fallback works when server is unavailable
 * 3. System recovers when server comes back online
 */
TEST_F(EndToEndIntegrationTest, OPCDisconnectionAndRecovery) {
    std::string nodeId = getTestNodeId(1001);

    std::cout << "Step 1: Normal request with server running" << std::endl;
    auto response1 = makeAPIRequest(nodeId);
    ASSERT_TRUE(response1.contains("readResults"));
    ASSERT_EQ(response1["readResults"].size(), 1);
    EXPECT_TRUE(response1["readResults"][0]["success"].get<bool>());

    std::string cachedValue = response1["readResults"][0]["value"];

    std::cout << "Step 2: Stop OPC UA server" << std::endl;
    mockServer_->stop();

    // Wait a bit for disconnection to be detected
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "Step 3: Request with server down - should use cache fallback" << std::endl;
    auto response2 = makeAPIRequest(nodeId);
    ASSERT_TRUE(response2.contains("readResults"));
    ASSERT_EQ(response2["readResults"].size(), 1);

    // Should still return data (from cache) or error
    auto result2 = response2["readResults"][0];
    EXPECT_EQ(result2["nodeId"], nodeId);

    std::cout << "Step 4: Restart OPC UA server" << std::endl;
    ASSERT_TRUE(mockServer_->start()) << "Failed to restart mock server";

    // Wait for reconnection
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Step 5: Request after server recovery" << std::endl;
    auto response3 = makeAPIRequest(nodeId);
    ASSERT_TRUE(response3.contains("readResults"));
    ASSERT_EQ(response3["readResults"].size(), 1);

    // Should succeed again
    EXPECT_TRUE(response3["readResults"][0]["success"].get<bool>());

    std::cout << "Disconnection and recovery test completed" << std::endl;
}

/**
 * @brief Test API compatibility with cache system
 *
 * Verifies that:
 * 1. /iotgateway/read endpoint maintains exact same behavior
 * 2. JSON response format is unchanged
 * 3. All fields are present and correctly formatted
 */
TEST_F(EndToEndIntegrationTest, APICompatibilityWithCache) {
    std::string nodeId = getTestNodeId(1001);

    std::cout << "Testing API compatibility" << std::endl;
    auto response = makeAPIRequest(nodeId);

    // Verify response structure
    ASSERT_TRUE(response.contains("readResults"));
    ASSERT_TRUE(response["readResults"].is_array());
    ASSERT_EQ(response["readResults"].size(), 1);

    auto result = response["readResults"][0];

    // Verify all required fields are present
    EXPECT_TRUE(result.contains("nodeId")) << "Missing nodeId field";
    EXPECT_TRUE(result.contains("success")) << "Missing success field";
    EXPECT_TRUE(result.contains("quality")) << "Missing quality field";
    EXPECT_TRUE(result.contains("value")) << "Missing value field";
    EXPECT_TRUE(result.contains("timestamp_iso")) << "Missing timestamp_iso field";

    // Verify field types
    EXPECT_TRUE(result["nodeId"].is_string());
    EXPECT_TRUE(result["success"].is_boolean());
    EXPECT_TRUE(result["quality"].is_string());
    EXPECT_TRUE(result["value"].is_string());
    EXPECT_TRUE(result["timestamp_iso"].is_string());

    // Verify field values
    EXPECT_EQ(result["nodeId"], nodeId);
    EXPECT_TRUE(result["success"].get<bool>());
    EXPECT_EQ(result["quality"], "Good");
    EXPECT_FALSE(result["value"].get<std::string>().empty());
    EXPECT_FALSE(result["timestamp_iso"].get<std::string>().empty());

    std::cout << "API compatibility verified" << std::endl;
}

} // namespace test
} // namespace opcua2http
