#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

#include <crow.h>
#include <crow/middlewares/cors.h>
#include <nlohmann/json.hpp>

#include "config/Configuration.h"
#include "cache/CacheManager.h"
#include "subscription/SubscriptionManager.h"
#include "opcua/OPCUAClient.h"
#include "core/ReadResult.h"

namespace opcua2http {

/**
 * @brief HTTP API handler for the OPC UA to HTTP bridge
 * 
 * This class implements the REST API endpoints using the Crow framework.
 * It handles authentication, CORS, request parsing, and response formatting.
 * The main endpoint is /iotgateway/read for reading OPC UA node values.
 */
class APIHandler {
public:
    /**
     * @brief Authentication result structure
     */
    struct AuthResult {
        bool success;           // Whether authentication succeeded
        std::string reason;     // Reason for failure (if any)
        std::string method;     // Authentication method used
        
        static AuthResult createSuccess(const std::string& method) {
            return AuthResult{true, "", method};
        }
        
        static AuthResult createFailure(const std::string& reason) {
            return AuthResult{false, reason, ""};
        }
    };
    
    /**
     * @brief Request statistics for monitoring
     */
    struct RequestStats {
        uint64_t totalRequests;         // Total requests processed
        uint64_t successfulRequests;    // Successful requests
        uint64_t failedRequests;        // Failed requests
        uint64_t authenticationFailures; // Authentication failures
        uint64_t validationErrors;      // Request validation errors
        uint64_t cacheHits;            // Requests served from cache
        uint64_t cacheMisses;          // Requests requiring OPC UA reads
        std::chrono::steady_clock::time_point startTime; // Handler start time
        std::chrono::steady_clock::time_point lastRequest; // Last request time
        double averageResponseTimeMs;   // Average response time in milliseconds
    };

    /**
     * @brief Constructor
     * @param cacheManager Pointer to cache manager (must remain valid during lifetime)
     * @param subscriptionManager Pointer to subscription manager (must remain valid during lifetime)
     * @param opcClient Pointer to OPC UA client (must remain valid during lifetime)
     * @param config Configuration settings
     */
    APIHandler(CacheManager* cacheManager, 
               SubscriptionManager* subscriptionManager, 
               OPCUAClient* opcClient, 
               const Configuration& config);
    
    /**
     * @brief Destructor
     */
    ~APIHandler() = default;
    
    // Disable copy constructor and assignment operator
    APIHandler(const APIHandler&) = delete;
    APIHandler& operator=(const APIHandler&) = delete;
    
    /**
     * @brief Set up all routes in the Crow application
     * @param app Crow application instance to configure
     */
    void setupRoutes(crow::App<crow::CORSHandler>& app);
    
    /**
     * @brief Handle the main /iotgateway/read endpoint
     * @param req HTTP request object
     * @return HTTP response with JSON data or error
     */
    crow::response handleReadRequest(const crow::request& req);
    
    /**
     * @brief Handle health check endpoint
     * @return HTTP response with system health information
     */
    crow::response handleHealthRequest();
    
    /**
     * @brief Handle status endpoint
     * @return HTTP response with detailed system status
     */
    crow::response handleStatusRequest();
    
    /**
     * @brief Authenticate HTTP request
     * @param req HTTP request to authenticate
     * @return AuthResult indicating success/failure and method used
     */
    AuthResult authenticateRequest(const crow::request& req);
    

    
    /**
     * @brief Get request statistics
     * @return RequestStats structure with current statistics
     */
    RequestStats getStats() const;
    
    /**
     * @brief Reset request statistics
     */
    void resetStats();
    
    /**
     * @brief Enable or disable detailed request logging
     * @param enabled Whether detailed logging should be enabled
     */
    void setDetailedLoggingEnabled(bool enabled);
    
    /**
     * @brief Check if detailed logging is enabled
     * @return True if detailed logging is enabled
     */
    bool isDetailedLoggingEnabled() const;

protected:
    // Authentication helper methods (protected for testing)
    
    /**
     * @brief Validate API key authentication
     * @param apiKey API key from request header
     * @return True if API key is valid, false otherwise
     */
    bool validateAPIKey(const std::string& apiKey);
    
    /**
     * @brief Validate HTTP Basic Authentication
     * @param authHeader Authorization header value
     * @return True if credentials are valid, false otherwise
     */
    bool validateBasicAuth(const std::string& authHeader);
    
    /**
     * @brief Rate limiting for authentication attempts
     * @param clientIP Client IP address
     * @return True if request is allowed, false if rate limited
     */
    bool checkRateLimit(const std::string& clientIP);
    
    /**
     * @brief Record failed authentication attempt
     * @param clientIP Client IP address
     */
    void recordFailedAuth(const std::string& clientIP);
    
    /**
     * @brief Check if IP is temporarily blocked due to failed attempts
     * @param clientIP Client IP address
     * @return True if IP is blocked, false otherwise
     */
    bool isIPBlocked(const std::string& clientIP);
    

    
    /**
     * @brief Build JSON response
     * @param data JSON data to return
     * @param statusCode HTTP status code (default: 200)
     * @return HTTP response with JSON content
     */
    crow::response buildJSONResponse(const nlohmann::json& data, int statusCode = 200);
    
    /**
     * @brief Build error response
     * @param statusCode HTTP status code
     * @param message Error message
     * @param details Optional additional error details
     * @return HTTP response with error information
     */
    crow::response buildErrorResponse(int statusCode, 
                                    const std::string& message, 
                                    const std::string& details = "");
    

    

    
    /**
     * @brief Build JSON response for read results
     * @param results Vector of ReadResult structures
     * @return JSON object with readResults array
     */
    nlohmann::json buildReadResponse(const std::vector<ReadResult>& results);
    
    /**
     * @brief Build paginated response for large result sets
     * @param results Vector of ReadResult structures
     * @param page Page number (0-based)
     * @param pageSize Number of results per page
     * @return JSON object with paginated results
     */
    nlohmann::json buildPaginatedResponse(const std::vector<ReadResult>& results, 
                                        int page = 0, int pageSize = 100);
    
    /**
     * @brief Build response with metadata
     * @param results Vector of ReadResult structures
     * @param includeMetadata Whether to include additional metadata
     * @return JSON object with results and metadata
     */
    nlohmann::json buildResponseWithMetadata(const std::vector<ReadResult>& results, 
                                           bool includeMetadata = true);
    
    /**
     * @brief Format timestamp as ISO 8601 string
     * @param timestamp Unix timestamp in milliseconds
     * @return ISO 8601 formatted timestamp string
     */
    std::string formatTimestamp(uint64_t timestamp);

private:
    // Core components
    CacheManager* cacheManager_;                    // Cache manager reference
    SubscriptionManager* subscriptionManager_;     // Subscription manager reference
    OPCUAClient* opcClient_;                       // OPC UA client reference
    Configuration config_;                         // Configuration settings
    
    // Statistics (atomic for thread-safe access)
    mutable std::atomic<uint64_t> totalRequests_{0};
    mutable std::atomic<uint64_t> successfulRequests_{0};
    mutable std::atomic<uint64_t> failedRequests_{0};
    mutable std::atomic<uint64_t> authenticationFailures_{0};
    mutable std::atomic<uint64_t> validationErrors_{0};
    mutable std::atomic<uint64_t> cacheHits_{0};
    mutable std::atomic<uint64_t> cacheMisses_{0};
    std::chrono::steady_clock::time_point startTime_;
    mutable std::atomic<std::chrono::steady_clock::time_point> lastRequest_;
    mutable std::atomic<double> averageResponseTimeMs_{0.0};
    
    // Configuration
    std::atomic<bool> detailedLoggingEnabled_{false};
    
    // Private helper methods
    
    /**
     * @brief Parse node IDs from query parameter
     * @param idsParam Comma-separated node IDs parameter
     * @return Vector of individual node ID strings
     */
    std::vector<std::string> parseNodeIds(const std::string& idsParam);
    
    /**
     * @brief Process a single node ID request
     * @param nodeId Node ID to process
     * @param cacheHit Output parameter indicating if request was served from cache
     * @return ReadResult for the node
     */
    ReadResult processNodeRequest(const std::string& nodeId, bool& cacheHit);
    
    /**
     * @brief Process multiple node ID requests
     * @param nodeIds Vector of node IDs to process
     * @return Vector of ReadResult structures
     */
    std::vector<ReadResult> processNodeRequests(const std::vector<std::string>& nodeIds);
    
    // Authentication helper methods
    
    /**
     * @brief Extract API key from request headers
     * @param req HTTP request
     * @return API key string, empty if not found
     */
    std::string extractAPIKey(const crow::request& req);
    
    /**
     * @brief Extract Authorization header from request
     * @param req HTTP request
     * @return Authorization header value, empty if not found
     */
    std::string extractAuthHeader(const crow::request& req);
    
    /**
     * @brief Decode Base64 string (for Basic Auth)
     * @param encoded Base64 encoded string
     * @return Decoded string
     */
    std::string decodeBase64(const std::string& encoded);
    
    // Request validation methods
    
    /**
     * @brief Validate request parameters
     * @param req HTTP request to validate
     * @return True if request is valid, false otherwise
     */
    bool validateRequest(const crow::request& req);
    
    /**
     * @brief Validate node ID format
     * @param nodeId Node ID to validate
     * @return True if format is valid, false otherwise
     */
    bool validateNodeId(const std::string& nodeId);
    
    /**
     * @brief Check if request origin is allowed (CORS)
     * @param origin Origin header value
     * @return True if origin is allowed, false otherwise
     */
    bool isOriginAllowed(const std::string& origin);
    
    // Utility methods
    
    /**
     * @brief Get current timestamp in milliseconds
     * @return Unix timestamp in milliseconds
     */
    uint64_t getCurrentTimestamp();
    
    /**
     * @brief Update request statistics
     * @param success Whether request was successful
     * @param responseTimeMs Response time in milliseconds
     * @param cacheHit Whether request was served from cache
     */
    void updateStats(bool success, double responseTimeMs, bool cacheHit = false);
    
    /**
     * @brief Log request details
     * @param req HTTP request
     * @param response HTTP response
     * @param responseTimeMs Response time in milliseconds
     */
    void logRequest(const crow::request& req, const crow::response& response, double responseTimeMs);
    
    /**
     * @brief Get client IP address from request
     * @param req HTTP request
     * @return Client IP address string
     */
    std::string getClientIP(const crow::request& req);
    
    /**
     * @brief Trim whitespace from string
     * @param str String to trim
     * @return Trimmed string
     */
    std::string trim(const std::string& str);
    
    /**
     * @brief Split string by delimiter
     * @param str String to split
     * @param delimiter Delimiter character
     * @return Vector of split strings
     */
    std::vector<std::string> split(const std::string& str, char delimiter);
    
    /**
     * @brief Convert string to lowercase
     * @param str String to convert
     * @return Lowercase string
     */
    std::string toLowerCase(const std::string& str);
    
    /**
     * @brief URL decode string
     * @param str URL encoded string
     * @return Decoded string
     */
    std::string urlDecode(const std::string& str);
    
    /**
     * @brief Check if string is empty or contains only whitespace
     * @param str String to check
     * @return True if string is empty or whitespace only
     */
    bool isEmptyOrWhitespace(const std::string& str);
    
    /**
     * @brief Get error type string from HTTP status code
     * @param statusCode HTTP status code
     * @return Error type string
     */
    std::string getErrorType(int statusCode);
    
    /**
     * @brief Generate unique request ID for tracking
     * @return Unique request ID string
     */
    std::string generateRequestId();
    
    /**
     * @brief Synchronize cache and subscription states
     * @return Number of inconsistencies found and fixed
     */
    int synchronizeCacheAndSubscriptions();
    
    /**
     * @brief Handle subscription recovery after reconnection
     * @return True if recovery was successful, false otherwise
     */
    bool handleSubscriptionRecovery();
    
private:
    // Rate limiting data structures
    struct RateLimitInfo {
        std::chrono::steady_clock::time_point lastAttempt;
        int failedAttempts;
        std::chrono::steady_clock::time_point blockUntil;
    };
    
    mutable std::mutex rateLimitMutex_;
    std::unordered_map<std::string, RateLimitInfo> rateLimitMap_;
};

} // namespace opcua2http