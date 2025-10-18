#include "http/APIHandler.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <regex>
#include <cstring>

namespace opcua2http {

APIHandler::APIHandler(CacheManager* cacheManager, 
                      SubscriptionManager* subscriptionManager, 
                      OPCUAClient* opcClient, 
                      const Configuration& config)
    : cacheManager_(cacheManager)
    , subscriptionManager_(subscriptionManager)
    , opcClient_(opcClient)
    , config_(config)
    , startTime_(std::chrono::steady_clock::now())
{
    if (!cacheManager_) {
        throw std::invalid_argument("CacheManager cannot be null");
    }
    if (!subscriptionManager_) {
        throw std::invalid_argument("SubscriptionManager cannot be null");
    }
    if (!opcClient_) {
        throw std::invalid_argument("OPCUAClient cannot be null");
    }
    
    std::cout << "APIHandler initialized with endpoint: " << config_.opcEndpoint 
              << ", port: " << config_.serverPort << std::endl;
}

void APIHandler::setupRoutes(crow::SimpleApp& app) {
    // Set up CORS first
    setupCORS(app);
    
    // Main API endpoint for reading OPC UA data
    CROW_ROUTE(app, "/iotgateway/read")
    .methods("GET"_method)
    ([this](const crow::request& req) {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Authenticate request
        AuthResult authResult = authenticateRequest(req);
        if (!authResult.success) {
            authenticationFailures_++;
            auto response = buildErrorResponse(401, "Unauthorized", authResult.reason);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            double responseTimeMs = duration.count() / 1000.0;
            
            updateStats(false, responseTimeMs);
            logRequest(req, response, responseTimeMs);
            return response;
        }
        
        // Handle the read request
        auto response = handleReadRequest(req);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        double responseTimeMs = duration.count() / 1000.0;
        
        bool success = (response.code >= 200 && response.code < 300);
        updateStats(success, responseTimeMs);
        logRequest(req, response, responseTimeMs);
        
        return response;
    });
    
    // Health check endpoint
    CROW_ROUTE(app, "/health")
    ([this]() {
        return handleHealthRequest();
    });
    
    // Status endpoint with detailed information
    CROW_ROUTE(app, "/status")
    ([this]() {
        return handleStatusRequest();
    });
    
    // Options endpoint for CORS preflight
    CROW_ROUTE(app, "/iotgateway/read")
    .methods("OPTIONS"_method)
    ([this](const crow::request& req) {
        crow::response response(200);
        
        // Add CORS headers
        std::string origin = req.get_header_value("Origin");
        addCORSHeaders(response, origin);
        
        return response;
    });
    
    std::cout << "API routes configured successfully" << std::endl;
}

crow::response APIHandler::handleReadRequest(const crow::request& req) {
    totalRequests_++;
    
    try {
        // Validate request
        if (!validateRequest(req)) {
            validationErrors_++;
            return buildErrorResponse(400, "Bad Request", "Invalid request parameters");
        }
        
        // Extract node IDs from query parameter
        std::string idsParam = req.url_params.get("ids");
        if (idsParam.empty()) {
            validationErrors_++;
            return buildErrorResponse(400, "Bad Request", "Missing 'ids' parameter");
        }
        
        // Parse node IDs
        std::vector<std::string> nodeIds = parseNodeIds(idsParam);
        if (nodeIds.empty()) {
            validationErrors_++;
            return buildErrorResponse(400, "Bad Request", "No valid node IDs provided");
        }
        
        // Validate node IDs
        for (const auto& nodeId : nodeIds) {
            if (!validateNodeId(nodeId)) {
                validationErrors_++;
                return buildErrorResponse(400, "Bad Request", 
                    "Invalid node ID format: " + nodeId);
            }
        }
        
        // Process the requests
        std::vector<ReadResult> results = processNodeRequests(nodeIds);
        
        // Build response
        nlohmann::json responseData = buildReadResponse(results);
        
        successfulRequests_++;
        return buildJSONResponse(responseData);
        
    } catch (const std::exception& e) {
        failedRequests_++;
        std::cerr << "Error handling read request: " << e.what() << std::endl;
        return buildErrorResponse(500, "Internal Server Error", e.what());
    }
}

crow::response APIHandler::handleHealthRequest() {
    try {
        nlohmann::json health = {
            {"status", "ok"},
            {"timestamp", getCurrentTimestamp()},
            {"uptime_seconds", std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime_).count()},
            {"opc_connected", opcClient_->isConnected()},
            {"opc_endpoint", config_.opcEndpoint},
            {"cached_items", cacheManager_->size()},
            {"active_subscriptions", subscriptionManager_->getActiveMonitoredItems().size()},
            {"version", "1.0.0"}
        };
        
        return buildJSONResponse(health);
        
    } catch (const std::exception& e) {
        std::cerr << "Error handling health request: " << e.what() << std::endl;
        return buildErrorResponse(500, "Internal Server Error", e.what());
    }
}

crow::response APIHandler::handleStatusRequest() {
    try {
        auto stats = getStats();
        auto cacheStats = cacheManager_->getStats();
        auto subscriptionStats = subscriptionManager_->getStats();
        
        nlohmann::json status = {
            {"timestamp", getCurrentTimestamp()},
            {"uptime_seconds", std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime_).count()},
            {"opc_ua", {
                {"connected", opcClient_->isConnected()},
                {"endpoint", config_.opcEndpoint},
                {"connection_state", static_cast<int>(opcClient_->getConnectionState())},
                {"connection_info", opcClient_->getConnectionInfo()}
            }},
            {"cache", {
                {"total_entries", cacheStats.totalEntries},
                {"subscribed_entries", cacheStats.subscribedEntries},
                {"total_hits", cacheStats.totalHits},
                {"total_misses", cacheStats.totalMisses},
                {"hit_ratio", cacheStats.hitRatio},
                {"memory_usage_bytes", cacheStats.memoryUsageBytes}
            }},
            {"subscriptions", {
                {"subscription_id", subscriptionStats.subscriptionId},
                {"total_monitored_items", subscriptionStats.totalMonitoredItems},
                {"active_monitored_items", subscriptionStats.activeMonitoredItems},
                {"total_notifications", subscriptionStats.totalNotifications},
                {"is_active", subscriptionStats.isSubscriptionActive}
            }},
            {"http_api", {
                {"total_requests", stats.totalRequests},
                {"successful_requests", stats.successfulRequests},
                {"failed_requests", stats.failedRequests},
                {"authentication_failures", stats.authenticationFailures},
                {"validation_errors", stats.validationErrors},
                {"cache_hits", stats.cacheHits},
                {"cache_misses", stats.cacheMisses},
                {"average_response_time_ms", stats.averageResponseTimeMs}
            }}
        };
        
        return buildJSONResponse(status);
        
    } catch (const std::exception& e) {
        std::cerr << "Error handling status request: " << e.what() << std::endl;
        return buildErrorResponse(500, "Internal Server Error", e.what());
    }
}

APIHandler::AuthResult APIHandler::authenticateRequest(const crow::request& req) {
    std::string clientIP = getClientIP(req);
    
    // Check rate limiting first
    if (!checkRateLimit(clientIP)) {
        return AuthResult::createFailure("Rate limit exceeded");
    }
    
    // Check if IP is blocked due to too many failed attempts
    if (isIPBlocked(clientIP)) {
        return AuthResult::createFailure("IP temporarily blocked");
    }
    
    // If no authentication is configured, allow all requests
    if (config_.apiKey.empty() && config_.authUsername.empty()) {
        return AuthResult::createSuccess("none");
    }
    
    bool authAttempted = false;
    
    // Try API Key authentication first
    if (!config_.apiKey.empty()) {
        std::string apiKey = extractAPIKey(req);
        if (!apiKey.empty()) {
            authAttempted = true;
            if (validateAPIKey(apiKey)) {
                return AuthResult::createSuccess("api_key");
            } else {
                recordFailedAuth(clientIP);
                return AuthResult::createFailure("Invalid API key");
            }
        }
    }
    
    // Try Basic Authentication
    if (!config_.authUsername.empty() && !config_.authPassword.empty()) {
        std::string authHeader = extractAuthHeader(req);
        if (!authHeader.empty()) {
            authAttempted = true;
            if (validateBasicAuth(authHeader)) {
                return AuthResult::createSuccess("basic_auth");
            } else {
                recordFailedAuth(clientIP);
                return AuthResult::createFailure("Invalid credentials");
            }
        }
    }
    
    // No valid authentication found
    if (authAttempted) {
        recordFailedAuth(clientIP);
    }
    return AuthResult::createFailure("Authentication required");
}

void APIHandler::setupCORS(crow::SimpleApp& /* app */) {
    // Crow doesn't have built-in CORS middleware, so we handle it manually in responses
    // This method is called to initialize CORS configuration
    
    if (!config_.allowedOrigins.empty()) {
        std::cout << "CORS configured with " << config_.allowedOrigins.size() 
                  << " specific allowed origins" << std::endl;
    } else {
        std::cout << "CORS configured to allow all origins" << std::endl;
    }
    
    std::cout << "CORS headers will be added to responses manually" << std::endl;
}

std::vector<std::string> APIHandler::parseNodeIds(const std::string& idsParam) {
    std::vector<std::string> nodeIds;
    
    if (idsParam.empty()) {
        return nodeIds;
    }
    
    // Split by comma and trim whitespace
    std::vector<std::string> parts = split(idsParam, ',');
    
    for (const auto& part : parts) {
        std::string trimmed = trim(part);
        if (!trimmed.empty()) {
            nodeIds.push_back(trimmed);
        }
    }
    
    return nodeIds;
}

nlohmann::json APIHandler::buildReadResponse(const std::vector<ReadResult>& results) {
    return buildResponseWithMetadata(results, true);
}

crow::response APIHandler::buildErrorResponse(int statusCode, 
                                            const std::string& message, 
                                            const std::string& details) {
    uint64_t timestamp = getCurrentTimestamp();
    
    nlohmann::json error = {
        {"error", {
            {"code", statusCode},
            {"message", message},
            {"timestamp", timestamp},
            {"timestamp_iso", formatTimestamp(timestamp)},
            {"type", getErrorType(statusCode)}
        }}
    };
    
    if (!details.empty()) {
        error["error"]["details"] = details;
    }
    
    // Add helpful information for common errors
    switch (statusCode) {
        case 400:
            error["error"]["help"] = "Check request parameters and format";
            break;
        case 401:
            error["error"]["help"] = "Provide valid authentication credentials";
            break;
        case 403:
            error["error"]["help"] = "Access denied - check permissions";
            break;
        case 404:
            error["error"]["help"] = "Resource not found";
            break;
        case 429:
            error["error"]["help"] = "Too many requests - please slow down";
            error["error"]["retry_after"] = 60; // seconds
            break;
        case 500:
            error["error"]["help"] = "Internal server error - please try again later";
            break;
        case 503:
            error["error"]["help"] = "Service temporarily unavailable";
            break;
    }
    
    // Add request ID for tracking
    error["error"]["request_id"] = generateRequestId();
    
    crow::response response = buildJSONResponse(error, statusCode);
    return response;
}

crow::response APIHandler::buildJSONResponse(const nlohmann::json& data, int statusCode) {
    crow::response response(statusCode);
    response.add_header("Content-Type", "application/json; charset=utf-8");
    response.write(data.dump());
    
    // Add security headers
    response.add_header("X-Content-Type-Options", "nosniff");
    response.add_header("X-Frame-Options", "DENY");
    response.add_header("X-XSS-Protection", "1; mode=block");
    response.add_header("Cache-Control", "no-cache, no-store, must-revalidate");
    response.add_header("Pragma", "no-cache");
    response.add_header("Expires", "0");
    
    // Add CORS headers
    addCORSHeaders(response, "");
    
    return response;
}

std::vector<ReadResult> APIHandler::processNodeRequests(const std::vector<std::string>& nodeIds) {
    std::vector<ReadResult> results;
    results.reserve(nodeIds.size());
    
    // Separate cached and non-cached nodes for batch processing
    std::vector<std::string> cachedNodes;
    std::vector<std::string> nonCachedNodes;
    std::vector<size_t> originalIndices;
    
    // First pass: check cache for all nodes
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        const auto& nodeId = nodeIds[i];
        auto cachedEntry = cacheManager_->getCachedValue(nodeId);
        
        if (cachedEntry.has_value()) {
            // Node is in cache
            cachedNodes.push_back(nodeId);
            results.push_back(cachedEntry->toReadResult());
            originalIndices.push_back(i);
            cacheHits_++;
            
            // Update last accessed time for subscription management
            subscriptionManager_->updateLastAccessed(nodeId);
            
            if (detailedLoggingEnabled_) {
                std::cout << "Cache hit for node: " << nodeId << std::endl;
            }
        } else {
            // Node not in cache, needs OPC UA read
            nonCachedNodes.push_back(nodeId);
            originalIndices.push_back(i);
            cacheMisses_++;
        }
    }
    
    // Second pass: batch read non-cached nodes from OPC UA server
    if (!nonCachedNodes.empty()) {
        if (detailedLoggingEnabled_) {
            std::cout << "Reading " << nonCachedNodes.size() 
                      << " nodes from OPC UA server" << std::endl;
        }
        
        // Use batch read for better performance
        std::vector<ReadResult> opcResults = opcClient_->readNodes(nonCachedNodes);
        
        // Process each OPC UA result
        for (size_t i = 0; i < opcResults.size() && i < nonCachedNodes.size(); ++i) {
            const auto& nodeId = nonCachedNodes[i];
            const auto& result = opcResults[i];
            
            // Add to cache
            cacheManager_->addCacheEntry(result, false);
            
            // Create subscription for successful reads
            if (result.success) {
                bool subscriptionCreated = subscriptionManager_->addMonitoredItem(nodeId);
                if (subscriptionCreated) {
                    // Update cache to indicate subscription exists
                    cacheManager_->setSubscriptionStatus(nodeId, true);
                    
                    if (detailedLoggingEnabled_) {
                        std::cout << "Created subscription for node: " << nodeId << std::endl;
                    }
                } else {
                    std::cerr << "Failed to create subscription for node: " << nodeId << std::endl;
                }
            }
            
            results.push_back(result);
        }
    }
    
    // Third pass: reorder results to match original request order
    std::vector<ReadResult> orderedResults(nodeIds.size());
    size_t cachedIndex = 0;
    size_t nonCachedIndex = cachedNodes.size();
    
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        const auto& nodeId = nodeIds[i];
        auto cachedEntry = cacheManager_->getCachedValue(nodeId);
        
        if (cachedEntry.has_value()) {
            orderedResults[i] = results[cachedIndex++];
        } else {
            if (nonCachedIndex < results.size()) {
                orderedResults[i] = results[nonCachedIndex++];
            } else {
                // Fallback for error cases
                orderedResults[i] = ReadResult::createError(nodeId, 
                    "Failed to read from OPC UA server", getCurrentTimestamp());
            }
        }
    }
    
    return orderedResults;
}

ReadResult APIHandler::processNodeRequest(const std::string& nodeId, bool& cacheHit) {
    cacheHit = false;
    
    try {
        // First, try to get from cache
        auto cachedEntry = cacheManager_->getCachedValue(nodeId);
        if (cachedEntry.has_value()) {
            cacheHit = true;
            
            // Update last accessed time for subscription management
            subscriptionManager_->updateLastAccessed(nodeId);
            
            // Check if subscription exists, create if needed
            if (!cachedEntry->hasSubscription && 
                !subscriptionManager_->hasMonitoredItem(nodeId)) {
                
                bool subscriptionCreated = subscriptionManager_->addMonitoredItem(nodeId);
                if (subscriptionCreated) {
                    cacheManager_->setSubscriptionStatus(nodeId, true);
                    
                    if (detailedLoggingEnabled_) {
                        std::cout << "Created missing subscription for cached node: " 
                                  << nodeId << std::endl;
                    }
                }
            }
            
            return cachedEntry->toReadResult();
        }
        
        // Not in cache, need to read from OPC UA server
        ReadResult result = opcClient_->readNode(nodeId);
        
        // Add to cache
        cacheManager_->addCacheEntry(result, false);
        
        // Create subscription for future updates (if successful read)
        if (result.success) {
            bool subscriptionCreated = subscriptionManager_->addMonitoredItem(nodeId);
            if (subscriptionCreated) {
                // Update cache to indicate subscription exists
                cacheManager_->setSubscriptionStatus(nodeId, true);
                
                if (detailedLoggingEnabled_) {
                    std::cout << "Created subscription for new node: " << nodeId << std::endl;
                }
            } else {
                std::cerr << "Failed to create subscription for node: " << nodeId << std::endl;
                
                // Even if subscription creation failed, we still have the read result
                // Log the issue but don't fail the request
                if (detailedLoggingEnabled_) {
                    std::cout << "Continuing without subscription for node: " << nodeId << std::endl;
                }
            }
        } else {
            if (detailedLoggingEnabled_) {
                std::cout << "Skipping subscription creation for failed read: " 
                          << nodeId << " (reason: " << result.reason << ")" << std::endl;
            }
        }
        
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing node request for " << nodeId << ": " << e.what() << std::endl;
        return ReadResult::createError(nodeId, "Internal error: " + std::string(e.what()), 
                                     getCurrentTimestamp());
    }
}

bool APIHandler::validateAPIKey(const std::string& apiKey) {
    return !config_.apiKey.empty() && apiKey == config_.apiKey;
}

bool APIHandler::validateBasicAuth(const std::string& authHeader) {
    if (config_.authUsername.empty() || config_.authPassword.empty()) {
        return false;
    }
    
    // Check if it starts with "Basic "
    const std::string basicPrefix = "Basic ";
    if (authHeader.length() <= basicPrefix.length() || 
        authHeader.substr(0, basicPrefix.length()) != basicPrefix) {
        return false;
    }
    
    // Extract and decode the credentials
    std::string encoded = authHeader.substr(basicPrefix.length());
    std::string decoded = decodeBase64(encoded);
    
    // Find the colon separator
    size_t colonPos = decoded.find(':');
    if (colonPos == std::string::npos) {
        return false;
    }
    
    std::string username = decoded.substr(0, colonPos);
    std::string password = decoded.substr(colonPos + 1);
    
    return username == config_.authUsername && password == config_.authPassword;
}

std::string APIHandler::extractAPIKey(const crow::request& req) {
    return req.get_header_value("X-API-Key");
}

std::string APIHandler::extractAuthHeader(const crow::request& req) {
    return req.get_header_value("Authorization");
}

std::string APIHandler::decodeBase64(const std::string& encoded) {
    // Simple Base64 decoder implementation
    // Note: In production, you'd want to use a more robust implementation
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string decoded;
    
    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        
        auto pos = chars.find(c);
        if (pos == std::string::npos) continue;
        
        val = (val << 6) + static_cast<int>(pos);
        valb += 6;
        if (valb >= 0) {
            decoded.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    return decoded;
}

bool APIHandler::validateRequest(const crow::request& req) {
    // Check if it's a GET request
    if (req.method != crow::HTTPMethod::Get) {
        return false;
    }
    
    // Check if ids parameter exists
    std::string idsParam = req.url_params.get("ids");
    return !idsParam.empty();
}

bool APIHandler::validateNodeId(const std::string& nodeId) {
    if (nodeId.empty()) {
        return false;
    }
    
    // Basic validation for OPC UA node ID format
    // Should match patterns like: ns=2;s=Variable1, ns=0;i=2253, etc.
    std::regex nodeIdPattern(R"(^ns=\d+;[si]=.+$)");
    return std::regex_match(nodeId, nodeIdPattern);
}

bool APIHandler::isOriginAllowed(const std::string& origin) {
    if (config_.allowedOrigins.empty()) {
        return true; // Allow all if no restrictions configured
    }
    
    return std::find(config_.allowedOrigins.begin(), config_.allowedOrigins.end(), origin) 
           != config_.allowedOrigins.end();
}

uint64_t APIHandler::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void APIHandler::updateStats(bool success, double responseTimeMs, bool /* cacheHit */) {
    if (success) {
        successfulRequests_++;
    } else {
        failedRequests_++;
    }
    
    // Update average response time using exponential moving average
    double currentAvg = averageResponseTimeMs_.load();
    double newAvg = currentAvg == 0.0 ? responseTimeMs : (currentAvg * 0.9 + responseTimeMs * 0.1);
    averageResponseTimeMs_.store(newAvg);
    
    lastRequest_.store(std::chrono::steady_clock::now());
}

void APIHandler::logRequest(const crow::request& req, const crow::response& response, double responseTimeMs) {
    if (!detailedLoggingEnabled_) {
        return;
    }
    
    std::string methodStr;
    switch (req.method) {
        case crow::HTTPMethod::Get: methodStr = "GET"; break;
        case crow::HTTPMethod::Post: methodStr = "POST"; break;
        case crow::HTTPMethod::Put: methodStr = "PUT"; break;
        case crow::HTTPMethod::Delete: methodStr = "DELETE"; break;
        case crow::HTTPMethod::Head: methodStr = "HEAD"; break;
        case crow::HTTPMethod::Options: methodStr = "OPTIONS"; break;
        default: methodStr = "UNKNOWN"; break;
    }
    
    std::cout << "[" << getCurrentTimestamp() << "] "
              << methodStr << " " << req.url << " "
              << response.code << " "
              << std::fixed << std::setprecision(2) << responseTimeMs << "ms "
              << "from " << getClientIP(req) << std::endl;
}

std::string APIHandler::getClientIP(const crow::request& req) {
    // Try to get real IP from headers (in case of proxy)
    std::string xForwardedFor = req.get_header_value("X-Forwarded-For");
    if (!xForwardedFor.empty()) {
        // Take the first IP in the list
        size_t commaPos = xForwardedFor.find(',');
        return commaPos != std::string::npos ? 
               trim(xForwardedFor.substr(0, commaPos)) : trim(xForwardedFor);
    }
    
    std::string xRealIP = req.get_header_value("X-Real-IP");
    if (!xRealIP.empty()) {
        return trim(xRealIP);
    }
    
    // Fall back to remote address (may not be available in all Crow versions)
    return "unknown";
}

APIHandler::RequestStats APIHandler::getStats() const {
    return RequestStats{
        totalRequests_.load(),
        successfulRequests_.load(),
        failedRequests_.load(),
        authenticationFailures_.load(),
        validationErrors_.load(),
        cacheHits_.load(),
        cacheMisses_.load(),
        startTime_,
        lastRequest_.load(),
        averageResponseTimeMs_.load()
    };
}

void APIHandler::resetStats() {
    totalRequests_.store(0);
    successfulRequests_.store(0);
    failedRequests_.store(0);
    authenticationFailures_.store(0);
    validationErrors_.store(0);
    cacheHits_.store(0);
    cacheMisses_.store(0);
    averageResponseTimeMs_.store(0.0);
    startTime_ = std::chrono::steady_clock::now();
}

void APIHandler::setDetailedLoggingEnabled(bool enabled) {
    detailedLoggingEnabled_.store(enabled);
}

bool APIHandler::isDetailedLoggingEnabled() const {
    return detailedLoggingEnabled_.load();
}

// Utility functions

std::string APIHandler::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> APIHandler::split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    
    return tokens;
}

std::string APIHandler::toLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string APIHandler::urlDecode(const std::string& str) {
    std::string decoded;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int value;
            std::istringstream is(str.substr(i + 1, 2));
            if (is >> std::hex >> value) {
                decoded += static_cast<char>(value);
                i += 2;
            } else {
                decoded += str[i];
            }
        } else if (str[i] == '+') {
            decoded += ' ';
        } else {
            decoded += str[i];
        }
    }
    return decoded;
}

bool APIHandler::isEmptyOrWhitespace(const std::string& str) {
    return str.empty() || str.find_first_not_of(" \t\r\n") == std::string::npos;
}

bool APIHandler::checkRateLimit(const std::string& clientIP) {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto& info = rateLimitMap_[clientIP];
    
    // Allow up to 60 requests per minute per IP
    const auto timeWindow = std::chrono::minutes(1);
    const int maxRequests = 60;
    
    // Reset counter if time window has passed
    if (now - info.lastAttempt > timeWindow) {
        info.failedAttempts = 0;
        info.lastAttempt = now;
        return true;
    }
    
    // Check if we're within the rate limit
    if (info.failedAttempts < maxRequests) {
        info.lastAttempt = now;
        return true;
    }
    
    return false;
}

void APIHandler::recordFailedAuth(const std::string& clientIP) {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto& info = rateLimitMap_[clientIP];
    
    info.failedAttempts++;
    info.lastAttempt = now;
    
    // Block IP for 15 minutes after 5 failed attempts
    const int maxFailedAttempts = 5;
    const auto blockDuration = std::chrono::minutes(15);
    
    if (info.failedAttempts >= maxFailedAttempts) {
        info.blockUntil = now + blockDuration;
        
        if (detailedLoggingEnabled_) {
            std::cout << "IP " << clientIP << " blocked for " 
                      << std::chrono::duration_cast<std::chrono::minutes>(blockDuration).count() 
                      << " minutes due to " << info.failedAttempts << " failed attempts" << std::endl;
        }
    }
}

bool APIHandler::isIPBlocked(const std::string& clientIP) {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    
    auto it = rateLimitMap_.find(clientIP);
    if (it == rateLimitMap_.end()) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    if (now < it->second.blockUntil) {
        return true;
    }
    
    // Block period has expired, reset failed attempts
    if (now >= it->second.blockUntil && it->second.failedAttempts > 0) {
        it->second.failedAttempts = 0;
        it->second.blockUntil = std::chrono::steady_clock::time_point{};
    }
    
    return false;
}

std::string APIHandler::formatTimestamp(uint64_t timestamp) {
    auto timePoint = std::chrono::system_clock::from_time_t(timestamp / 1000);
    auto ms = timestamp % 1000;
    
    std::time_t time = std::chrono::system_clock::to_time_t(timePoint);
    
#ifdef _WIN32
    std::tm tm_buf;
    gmtime_s(&tm_buf, &time);
    std::tm* tm = &tm_buf;
#else
    std::tm* tm = std::gmtime(&time);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms << "Z";
    
    return oss.str();
}

nlohmann::json APIHandler::buildPaginatedResponse(const std::vector<ReadResult>& results, 
                                                int page, int pageSize) {
    nlohmann::json response;
    
    // Calculate pagination
    int totalResults = static_cast<int>(results.size());
    int totalPages = (totalResults + pageSize - 1) / pageSize;
    int startIndex = page * pageSize;
    int endIndex = std::min(startIndex + pageSize, totalResults);
    
    // Build paginated results
    nlohmann::json readResults = nlohmann::json::array();
    for (int i = startIndex; i < endIndex; ++i) {
        // Create JSON with full field names for API response
        nlohmann::json resultJson = {
            {"nodeId", results[i].id},
            {"success", results[i].success},
            {"reason", results[i].reason},
            {"value", results[i].value},
            {"timestamp", results[i].timestamp}
        };
        
        // Add formatted timestamp for better readability
        resultJson["timestamp_iso"] = formatTimestamp(results[i].timestamp);
        
        readResults.push_back(resultJson);
    }
    
    // Build response with pagination metadata
    response["readResults"] = readResults;
    response["pagination"] = {
        {"page", page},
        {"page_size", pageSize},
        {"total_results", totalResults},
        {"total_pages", totalPages},
        {"has_next", page < totalPages - 1},
        {"has_previous", page > 0}
    };
    response["timestamp"] = getCurrentTimestamp();
    response["timestamp_iso"] = formatTimestamp(getCurrentTimestamp());
    response["count"] = endIndex - startIndex;
    
    return response;
}

nlohmann::json APIHandler::buildResponseWithMetadata(const std::vector<ReadResult>& results, 
                                                   bool includeMetadata) {
    nlohmann::json response;
    nlohmann::json readResults = nlohmann::json::array();
    
    // Statistics for metadata
    int successCount = 0;
    int errorCount = 0;
    std::map<std::string, int> statusCounts;
    
    for (const auto& result : results) {
        // Create JSON with full field names for API response
        nlohmann::json resultJson = {
            {"nodeId", result.id},
            {"success", result.success},
            {"reason", result.reason},
            {"value", result.value},
            {"timestamp", result.timestamp}
        };
        
        // Add formatted timestamp for better readability
        resultJson["timestamp_iso"] = formatTimestamp(result.timestamp);
        
        // Add quality indicator
        if (result.success) {
            resultJson["quality"] = "good";
            successCount++;
        } else {
            resultJson["quality"] = "bad";
            errorCount++;
        }
        
        readResults.push_back(resultJson);
        
        // Count status codes for metadata
        if (includeMetadata) {
            statusCounts[result.reason]++;
        }
    }
    
    response["readResults"] = readResults;
    response["timestamp"] = getCurrentTimestamp();
    response["timestamp_iso"] = formatTimestamp(getCurrentTimestamp());
    response["count"] = results.size();
    
    if (includeMetadata) {
        response["metadata"] = {
            {"success_count", successCount},
            {"error_count", errorCount},
            {"success_rate", results.empty() ? 0.0 : 
                static_cast<double>(successCount) / results.size()},
            {"status_breakdown", statusCounts},
            {"server_info", {
                {"opc_endpoint", config_.opcEndpoint},
                {"opc_connected", opcClient_->isConnected()},
                {"cache_size", cacheManager_->size()},
                {"active_subscriptions", subscriptionManager_->getActiveMonitoredItems().size()}
            }}
        };
    }
    
    return response;
}

std::string APIHandler::getErrorType(int statusCode) {
    switch (statusCode / 100) {
        case 4:
            switch (statusCode) {
                case 400: return "bad_request";
                case 401: return "unauthorized";
                case 403: return "forbidden";
                case 404: return "not_found";
                case 429: return "rate_limited";
                default: return "client_error";
            }
        case 5:
            switch (statusCode) {
                case 500: return "internal_error";
                case 502: return "bad_gateway";
                case 503: return "service_unavailable";
                case 504: return "gateway_timeout";
                default: return "server_error";
            }
        default:
            return "unknown_error";
    }
}

std::string APIHandler::generateRequestId() {
    // Generate a simple request ID using timestamp and random component
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    
    // Add some randomness
    static std::atomic<uint32_t> counter{0};
    uint32_t requestCounter = counter.fetch_add(1);
    
    std::ostringstream oss;
    oss << std::hex << timestamp << "-" << std::hex << requestCounter;
    return oss.str();
}

int APIHandler::synchronizeCacheAndSubscriptions() {
    int inconsistenciesFixed = 0;
    
    try {
        // Get all cached node IDs
        std::vector<std::string> cachedNodes = cacheManager_->getCachedNodeIds();
        std::vector<std::string> subscribedNodes = subscriptionManager_->getActiveMonitoredItems();
        
        // Check for cached nodes without subscriptions
        for (const auto& nodeId : cachedNodes) {
            auto cachedEntry = cacheManager_->getCachedValue(nodeId);
            if (cachedEntry.has_value() && cachedEntry->hasSubscription) {
                // Cache says it has subscription, verify with subscription manager
                if (!subscriptionManager_->hasMonitoredItem(nodeId)) {
                    // Inconsistency: cache thinks there's a subscription but there isn't
                    cacheManager_->setSubscriptionStatus(nodeId, false);
                    inconsistenciesFixed++;
                    
                    if (detailedLoggingEnabled_) {
                        std::cout << "Fixed cache inconsistency: removed subscription flag for " 
                                  << nodeId << std::endl;
                    }
                }
            }
        }
        
        // Check for subscriptions without proper cache flags
        for (const auto& nodeId : subscribedNodes) {
            auto cachedEntry = cacheManager_->getCachedValue(nodeId);
            if (cachedEntry.has_value() && !cachedEntry->hasSubscription) {
                // Inconsistency: subscription exists but cache doesn't know
                cacheManager_->setSubscriptionStatus(nodeId, true);
                inconsistenciesFixed++;
                
                if (detailedLoggingEnabled_) {
                    std::cout << "Fixed cache inconsistency: added subscription flag for " 
                              << nodeId << std::endl;
                }
            }
        }
        
        if (inconsistenciesFixed > 0) {
            std::cout << "Synchronized cache and subscriptions: fixed " 
                      << inconsistenciesFixed << " inconsistencies" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error synchronizing cache and subscriptions: " << e.what() << std::endl;
    }
    
    return inconsistenciesFixed;
}

bool APIHandler::handleSubscriptionRecovery() {
    try {
        if (!opcClient_->isConnected()) {
            std::cerr << "Cannot recover subscriptions: OPC UA client not connected" << std::endl;
            return false;
        }
        
        // Get all nodes that should have subscriptions
        std::vector<std::string> subscribedNodes = cacheManager_->getSubscribedNodeIds();
        
        if (subscribedNodes.empty()) {
            if (detailedLoggingEnabled_) {
                std::cout << "No subscriptions to recover" << std::endl;
            }
            return true;
        }
        
        std::cout << "Recovering " << subscribedNodes.size() << " subscriptions..." << std::endl;
        
        // Recreate all monitored items
        bool success = subscriptionManager_->recreateAllMonitoredItems();
        
        if (success) {
            std::cout << "Successfully recovered all subscriptions" << std::endl;
            
            // Synchronize cache and subscription states
            synchronizeCacheAndSubscriptions();
            
            return true;
        } else {
            std::cerr << "Failed to recover some subscriptions" << std::endl;
            
            // Try to recover individual subscriptions
            int recovered = 0;
            for (const auto& nodeId : subscribedNodes) {
                if (subscriptionManager_->addMonitoredItem(nodeId)) {
                    recovered++;
                } else {
                    // Mark as not having subscription in cache
                    cacheManager_->setSubscriptionStatus(nodeId, false);
                }
            }
            
            std::cout << "Recovered " << recovered << " out of " 
                      << subscribedNodes.size() << " subscriptions" << std::endl;
            
            return recovered > 0;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error during subscription recovery: " << e.what() << std::endl;
        return false;
    }
}

void APIHandler::addCORSHeaders(crow::response& response, const std::string& origin) {
    if (!config_.allowedOrigins.empty()) {
        // Check if the origin is allowed
        if (!origin.empty() && isOriginAllowed(origin)) {
            response.add_header("Access-Control-Allow-Origin", origin);
            response.add_header("Access-Control-Allow-Credentials", "true");
        } else if (origin.empty()) {
            // No origin provided, allow all origins when no specific origin is given
            response.add_header("Access-Control-Allow-Origin", "*");
        } else {
            // Origin not allowed, don't add CORS headers
            return;
        }
    } else {
        // Allow all origins if no restrictions configured
        response.add_header("Access-Control-Allow-Origin", "*");
    }
    
    response.add_header("Access-Control-Allow-Methods", "GET, OPTIONS");
    response.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-API-Key");
    response.add_header("Access-Control-Max-Age", "86400");
}

} // namespace opcua2http