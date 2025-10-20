#include <gtest/gtest.h>
#include <crow.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <map>

#include "common/OPCUATestBase.h"
#include "http/APIHandler.h"
#include "config/Configuration.h"
#include "core/ReadResult.h"
#include "core/ReadStrategy.h"

using namespace opcua2http;
using namespace opcua2http::test;

namespace {

// Helper function to create a properly initialized HTTP request
crow::request createMockRequest(const std::string& url,
                               const std::map<std::string, std::string>& headers = {},
                               crow::HTTPMethod method = crow::HTTPMethod::Get) {
    crow::request req;
    req.method = method;

    // Parse URL and query parameters
    size_t queryPos = url.find('?');
    if (queryPos != std::string::npos) {
        req.url = url.substr(0, queryPos);
        std::string query = url.substr(queryPos + 1);
        req.url_params = crow::query_string(query);
    } else {
        req.url = url;
    }

    // Add headers manually to the headers map
    for (const auto& header : headers) {
        req.headers.emplace(header.first, header.second);
    }

    return req;
}

// Helper function to create test configuration
Configuration createTestConfig() {
    Configuration config;
    config.opcEndpoint = "opc.tcp://localhost:4840";
    config.serverPort = 3000;
    config.apiKey = "test-api-key";
    config.authUsername = "testuser";
    config.authPassword = "testpass";
    config.allowedOrigins = {"http://localhost:3000", "https://example.com"};
    config.cacheExpireMinutes = 60;
    config.subscriptionCleanupMinutes = 30;
    return config;
}

// Test APIHandler class that exposes protected and private methods for testing
class TestableAPIHandler : public APIHandler {
public:
    TestableAPIHandler(CacheManager* cacheManager,
                      ReadStrategy* readStrategy,
                      OPCUAClient* opcClient,
                      const Configuration& config)
        : APIHandler(cacheManager, readStrategy, opcClient, config) {}

    // Expose protected methods for testing
    using APIHandler::validateAPIKey;
    using APIHandler::validateBasicAuth;
    using APIHandler::buildJSONResponse;
    using APIHandler::buildErrorResponse;
    using APIHandler::formatTimestamp;

    // Note: Private methods are tested indirectly through public interface
};

} // anonymous namespace

class APIHandlerTest : public OPCUATestBase {
protected:
    void SetUp() override {
        OPCUATestBase::SetUp();

        config_ = createTestConfig();
        config_.opcEndpoint = mockServer_->getEndpoint();

        // Create real components for integration testing
        opcClient_ = createConnectedOPCClient();
        ASSERT_NE(opcClient_, nullptr);

        cacheManager_ = createCacheManager();
        readStrategy_ = std::make_unique<ReadStrategy>(
            cacheManager_.get(), opcClient_.get());

        apiHandler_ = std::make_unique<TestableAPIHandler>(
            cacheManager_.get(),
            readStrategy_.get(),
            opcClient_.get(),
            config_
        );
    }

    void TearDown() override {
        apiHandler_.reset();
        readStrategy_.reset();
        cacheManager_.reset();
        opcClient_.reset();
        OPCUATestBase::TearDown();
    }

    Configuration config_;
    std::unique_ptr<OPCUAClient> opcClient_;
    std::unique_ptr<CacheManager> cacheManager_;
    std::unique_ptr<ReadStrategy> readStrategy_;
    std::unique_ptr<TestableAPIHandler> apiHandler_;
};

// Test Authentication Functionality
TEST_F(APIHandlerTest, AuthenticateRequest_ValidAPIKey_ReturnsSuccess) {
    // Arrange
    auto request = createMockRequest("/iotgateway/read?ids=ns=2;s=Test",
                                   {{"X-API-Key", "test-api-key"}});

    // Act
    auto authResult = apiHandler_->authenticateRequest(request);

    // Assert
    EXPECT_TRUE(authResult.success);
    EXPECT_EQ(authResult.method, "api_key");
    EXPECT_TRUE(authResult.reason.empty());
}

TEST_F(APIHandlerTest, AuthenticateRequest_InvalidAPIKey_ReturnsFailure) {
    // Arrange
    auto request = createMockRequest("/iotgateway/read?ids=ns=2;s=Test",
                                   {{"X-API-Key", "wrong-api-key"}});

    // Act
    auto authResult = apiHandler_->authenticateRequest(request);

    // Assert
    EXPECT_FALSE(authResult.success);
    EXPECT_EQ(authResult.reason, "Invalid API key");
    EXPECT_TRUE(authResult.method.empty());
}

TEST_F(APIHandlerTest, AuthenticateRequest_ValidBasicAuth_ReturnsSuccess) {
    // Arrange
    // Base64 encode "testuser:testpass"
    std::string credentials = "dGVzdHVzZXI6dGVzdHBhc3M="; // testuser:testpass
    auto request = createMockRequest("/iotgateway/read?ids=ns=2;s=Test",
                                   {{"Authorization", "Basic " + credentials}});

    // Act
    auto authResult = apiHandler_->authenticateRequest(request);

    // Assert
    EXPECT_TRUE(authResult.success);
    EXPECT_EQ(authResult.method, "basic_auth");
    EXPECT_TRUE(authResult.reason.empty());
}

TEST_F(APIHandlerTest, AuthenticateRequest_InvalidBasicAuth_ReturnsFailure) {
    // Arrange
    // Base64 encode "wronguser:wrongpass"
    std::string credentials = "d3JvbmdVc2VyOndyb25nUGFzcw=="; // wronguser:wrongpass
    auto request = createMockRequest("/iotgateway/read?ids=ns=2;s=Test",
                                   {{"Authorization", "Basic " + credentials}});

    // Act
    auto authResult = apiHandler_->authenticateRequest(request);

    // Assert
    EXPECT_FALSE(authResult.success);
    EXPECT_EQ(authResult.reason, "Invalid credentials");
    EXPECT_TRUE(authResult.method.empty());
}

TEST_F(APIHandlerTest, AuthenticateRequest_NoAuthentication_ReturnsFailure) {
    // Arrange
    auto request = createMockRequest("/iotgateway/read?ids=ns=2;s=Test");

    // Act
    auto authResult = apiHandler_->authenticateRequest(request);

    // Assert
    EXPECT_FALSE(authResult.success);
    EXPECT_EQ(authResult.reason, "Authentication required");
    EXPECT_TRUE(authResult.method.empty());
}

TEST_F(APIHandlerTest, AuthenticateRequest_NoAuthConfigured_ReturnsSuccess) {
    // Arrange - Create config with no authentication
    Configuration noAuthConfig = config_;
    noAuthConfig.apiKey = "";
    noAuthConfig.authUsername = "";
    noAuthConfig.authPassword = "";

    TestableAPIHandler noAuthHandler(cacheManager_.get(),
                                   readStrategy_.get(),
                                   opcClient_.get(),
                                   noAuthConfig);

    auto request = createMockRequest("/iotgateway/read?ids=ns=2;s=Test");

    // Act
    auto authResult = noAuthHandler.authenticateRequest(request);

    // Assert
    EXPECT_TRUE(authResult.success);
    EXPECT_EQ(authResult.method, "none");
    EXPECT_TRUE(authResult.reason.empty());
}

// Test JSON Response Formats
TEST_F(APIHandlerTest, BuildJSONResponse_ValidData_ReturnsFormattedResponse) {
    // Arrange
    nlohmann::json testData = {
        {"test", "value"},
        {"number", 42}
    };

    // Act
    crow::response response = apiHandler_->buildJSONResponse(testData, 201);

    // Assert
    EXPECT_EQ(response.code, 201);
    EXPECT_EQ(response.get_header_value("Content-Type"), "application/json; charset=utf-8");

    // Check security headers
    EXPECT_EQ(response.get_header_value("X-Content-Type-Options"), "nosniff");
    EXPECT_EQ(response.get_header_value("X-Frame-Options"), "DENY");
    EXPECT_EQ(response.get_header_value("X-XSS-Protection"), "1; mode=block");
    EXPECT_EQ(response.get_header_value("Cache-Control"), "no-cache, no-store, must-revalidate");

    // Note: CORS headers are now added by Crow's CORSHandler middleware
    // and are not present in unit test responses (middleware not active in unit tests)

    nlohmann::json responseJson = nlohmann::json::parse(response.body);
    EXPECT_EQ(responseJson["test"], "value");
    EXPECT_EQ(responseJson["number"], 42);
}

TEST_F(APIHandlerTest, BuildErrorResponse_WithDetails_ReturnsFormattedError) {
    // Arrange
    int statusCode = 404;
    std::string message = "Not Found";
    std::string details = "The requested resource was not found";

    // Act
    crow::response response = apiHandler_->buildErrorResponse(statusCode, message, details);

    // Assert
    EXPECT_EQ(response.code, 404);

    nlohmann::json responseJson = nlohmann::json::parse(response.body);
    ASSERT_TRUE(responseJson.contains("error"));

    auto error = responseJson["error"];
    EXPECT_EQ(error["code"], 404);
    EXPECT_EQ(error["message"], "Not Found");
    EXPECT_EQ(error["details"], details);
    EXPECT_EQ(error["type"], "not_found");
    EXPECT_EQ(error["help"], "Resource not found");
    EXPECT_TRUE(error.contains("request_id"));
    EXPECT_TRUE(error.contains("timestamp"));
    EXPECT_TRUE(error.contains("timestamp_iso"));
}

// Test Utility Functions
TEST_F(APIHandlerTest, FormatTimestamp_ValidTimestamp_ReturnsISO8601) {
    // Test timestamp formatting
    uint64_t timestamp = 1609459200000; // 2021-01-01 00:00:00 UTC
    std::string formatted = apiHandler_->formatTimestamp(timestamp);

    EXPECT_TRUE(formatted.find("2021-01-01T00:00:00") != std::string::npos);
    EXPECT_TRUE(formatted.find("Z") != std::string::npos);
}

TEST_F(APIHandlerTest, HandleHealthRequest_ReturnsSystemHealth) {
    // Act
    crow::response response = apiHandler_->handleHealthRequest();

    // Assert
    EXPECT_EQ(response.code, 200);

    nlohmann::json responseJson = nlohmann::json::parse(response.body);
    EXPECT_EQ(responseJson["status"], "ok");
    EXPECT_TRUE(responseJson["opc_connected"]);
    EXPECT_TRUE(responseJson["opc_endpoint"].get<std::string>().find("localhost") != std::string::npos);
    EXPECT_GE(responseJson["cached_items"].get<size_t>(), 0);
    // Note: active_subscriptions field is not included in health response
    EXPECT_EQ(responseJson["version"], "1.0.0");
    EXPECT_TRUE(responseJson.contains("timestamp"));
    EXPECT_TRUE(responseJson.contains("uptime_seconds"));
}

TEST_F(APIHandlerTest, HandleStatusRequest_ReturnsDetailedStatus) {
    // Act
    crow::response response = apiHandler_->handleStatusRequest();

    // Assert
    EXPECT_EQ(response.code, 200);

    nlohmann::json responseJson = nlohmann::json::parse(response.body);

    // Check OPC UA section
    ASSERT_TRUE(responseJson.contains("opc_ua"));
    auto opcua = responseJson["opc_ua"];
    EXPECT_TRUE(opcua["connected"]);
    EXPECT_TRUE(opcua["endpoint"].get<std::string>().find("localhost") != std::string::npos);

    // Check cache section
    ASSERT_TRUE(responseJson.contains("cache"));
    auto cache = responseJson["cache"];
    EXPECT_GE(cache["total_entries"].get<size_t>(), 0);
    EXPECT_GE(cache["total_hits"].get<uint64_t>(), 0);
    EXPECT_GE(cache["total_misses"].get<uint64_t>(), 0);
    EXPECT_GE(cache["hit_ratio"].get<double>(), 0.0);
    EXPECT_GE(cache["memory_usage_bytes"].get<size_t>(), 0);

    // Note: subscriptions section is not included in status response

    // Check HTTP API section
    ASSERT_TRUE(responseJson.contains("http_api"));
    auto httpApi = responseJson["http_api"];
    EXPECT_TRUE(httpApi.contains("total_requests"));
    EXPECT_TRUE(httpApi.contains("successful_requests"));
    EXPECT_TRUE(httpApi.contains("failed_requests"));
}

// Test Request Statistics
TEST_F(APIHandlerTest, GetStats_ReturnsAccurateStatistics) {
    // Act
    auto stats = apiHandler_->getStats();

    // Assert - Initial stats should be zero or valid
    EXPECT_GE(stats.totalRequests, 0);
    EXPECT_GE(stats.successfulRequests, 0);
    EXPECT_GE(stats.failedRequests, 0);
    EXPECT_GE(stats.authenticationFailures, 0);
    EXPECT_GE(stats.validationErrors, 0);
    EXPECT_GE(stats.cacheHits, 0);
    EXPECT_GE(stats.cacheMisses, 0);
    EXPECT_GE(stats.averageResponseTimeMs, 0.0);
}

TEST_F(APIHandlerTest, ResetStats_ClearsAllStatistics) {
    // Act
    apiHandler_->resetStats();

    // Assert
    auto statsAfter = apiHandler_->getStats();
    EXPECT_EQ(statsAfter.totalRequests, 0);
    EXPECT_EQ(statsAfter.successfulRequests, 0);
    EXPECT_EQ(statsAfter.failedRequests, 0);
    EXPECT_EQ(statsAfter.authenticationFailures, 0);
    EXPECT_EQ(statsAfter.validationErrors, 0);
    EXPECT_EQ(statsAfter.cacheHits, 0);
    EXPECT_EQ(statsAfter.cacheMisses, 0);
    EXPECT_EQ(statsAfter.averageResponseTimeMs, 0.0);
}

// Note: CORS functionality is now handled by Crow's built-in CORSHandler middleware
// No unit tests needed as it's handled by the framework

// Test Detailed Logging
TEST_F(APIHandlerTest, SetDetailedLoggingEnabled_ChangesLoggingState) {
    // Arrange
    EXPECT_FALSE(apiHandler_->isDetailedLoggingEnabled());

    // Act
    apiHandler_->setDetailedLoggingEnabled(true);

    // Assert
    EXPECT_TRUE(apiHandler_->isDetailedLoggingEnabled());

    // Act
    apiHandler_->setDetailedLoggingEnabled(false);

    // Assert
    EXPECT_FALSE(apiHandler_->isDetailedLoggingEnabled());
}
