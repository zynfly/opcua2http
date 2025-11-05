#include "opcua/OPCUAClient.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>
#include <regex>
#include <chrono>
#include <cstring>
#include <algorithm>

// Additional open62541 includes for batch reading
#include <open62541/client_config_default.h>
#include <open62541/types_generated.h>

namespace opcua2http {

OPCUAClient::OPCUAClient()
    : client_(nullptr)
    , config_(nullptr)
    , connectionState_(ConnectionState::DISCONNECTED)
    , initialized_(false)
    , stateChangeCallback_(nullptr)
    , readTimeout_(std::chrono::milliseconds(5000))
    , connectionTimeout_(std::chrono::milliseconds(10000))
    , retryCount_(3)
    , batchSize_(50)
    , connectionHealthy_(false) {
}

OPCUAClient::~OPCUAClient() {
    disconnect();
    if (client_) {
        UA_Client_delete(client_);
        client_ = nullptr;
    }
}

bool OPCUAClient::initialize(const Configuration& config) {
    std::lock_guard<std::mutex> lock(clientMutex_);

    if (initialized_) {
        spdlog::error("OPCUAClient already initialized");
        return false;
    }

    appConfig_ = config;
    endpoint_ = config.opcEndpoint;

    // Configure batch reading and connection parameters
    readTimeout_ = std::chrono::milliseconds(config.opcReadTimeoutMs);
    connectionTimeout_ = std::chrono::milliseconds(config.opcConnectionTimeoutMs);
    batchSize_ = static_cast<size_t>(config.opcBatchSize);

    if (endpoint_.empty()) {
        spdlog::error("OPC UA endpoint is empty");
        return false;
    }

    client_ = UA_Client_new();
    if (!client_) {
        spdlog::error("Failed to create UA_Client");
        return false;
    }

    config_ = UA_Client_getConfig(client_);
    if (!config_) {
        spdlog::error("Failed to get client configuration");
        UA_Client_delete(client_);
        client_ = nullptr;
        return false;
    }

    UA_StatusCode status = UA_ClientConfig_setDefault(config_);
    if (status != UA_STATUSCODE_GOOD) {
        spdlog::error("Failed to set default client configuration: {}", statusCodeToString(status));
        UA_Client_delete(client_);
        client_ = nullptr;
        return false;
    }

    // Note: OPC UA logging integration will be added later

    if (!configureClientSecurity()) {
        spdlog::error("Failed to configure client security");
        UA_Client_delete(client_);
        client_ = nullptr;
        return false;
    }

    config_->stateCallback = stateCallback;
    config_->clientContext = this;
    config_->timeout = static_cast<UA_UInt32>(connectionTimeout_.count());

    initialized_ = true;
    updateConnectionState(ConnectionState::DISCONNECTED);

    spdlog::info("OPCUAClient initialized successfully for endpoint: {}", endpoint_);
    return true;
}

bool OPCUAClient::connect() {
    std::lock_guard<std::mutex> lock(clientMutex_);

    if (!initialized_) {
        spdlog::error("Client not initialized");
        return false;
    }

    if (connectionState_ == ConnectionState::CONNECTED) {
        return true;
    }

    updateConnectionState(ConnectionState::CONNECTING);
    lastConnectionAttempt_ = std::chrono::steady_clock::now();

    spdlog::info("Connecting to OPC UA server: {}", endpoint_);

    UA_StatusCode status = UA_Client_connect(client_, endpoint_.c_str());

    if (status == UA_STATUSCODE_GOOD) {
        updateConnectionState(ConnectionState::CONNECTED);
        connectionHealthy_ = true;
        setLastError(""); // Clear any previous errors
        spdlog::info("Successfully connected to OPC UA server");
        return true;
    } else {
        updateConnectionState(ConnectionState::CONNECTION_ERROR, status);
        connectionHealthy_ = false;
        setLastError("Connection failed: " + statusCodeToString(status));
        spdlog::error("Failed to connect to OPC UA server: {}", statusCodeToString(status));
        return false;
    }
}

void OPCUAClient::disconnect() {
    std::lock_guard<std::mutex> lock(clientMutex_);

    if (!initialized_ || !client_) {
        return;
    }

    if (connectionState_ == ConnectionState::CONNECTED ||
        connectionState_ == ConnectionState::CONNECTING) {

        spdlog::info("Disconnecting from OPC UA server");
        UA_Client_disconnect(client_);
        updateConnectionState(ConnectionState::DISCONNECTED);
        connectionHealthy_ = false;
    }
}

bool OPCUAClient::isConnected() const {
    return connectionState_ == ConnectionState::CONNECTED;
}

OPCUAClient::ConnectionState OPCUAClient::getConnectionState() const {
    return connectionState_;
}

UA_Client* OPCUAClient::getClient() const {
    return client_;
}

ReadResult OPCUAClient::readNode(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(clientMutex_);

    if (!isConnected()) {
        std::string error = "Client not connected";
        if (!lastError_.empty()) {
            error += " - " + lastError_;
        }
        setLastError(error);
        return ReadResult::createError(nodeId, error, getCurrentTimestamp());
    }

    if (!validateNodeIdFormat(nodeId)) {
        return ReadResult::createError(nodeId, "Invalid NodeId format", getCurrentTimestamp());
    }

    UA_NodeId uaNodeId = parseNodeId(nodeId);

    UA_Variant value;
    UA_Variant_init(&value);

    UA_StatusCode status = UA_Client_readValueAttribute(client_, uaNodeId, &value);

    ReadResult result;
    if (status == UA_STATUSCODE_GOOD) {
        // Create a DataValue with the variant and set hasValue flag
        UA_DataValue dataValue;
        UA_DataValue_init(&dataValue);
        dataValue.value = value;
        dataValue.hasValue = true;

        result = convertDataValue(nodeId, dataValue);

        // Don't clear dataValue.value since it's the same as 'value'
        // which will be cleared below
    } else {
        result = ReadResult::createError(nodeId, statusCodeToString(status), getCurrentTimestamp());
    }

    UA_Variant_clear(&value);
    UA_NodeId_clear(&uaNodeId);

    return result;
}

std::vector<ReadResult> OPCUAClient::readNodes(const std::vector<std::string>& nodeIds) {
    std::vector<ReadResult> results;
    results.reserve(nodeIds.size());

    if (!isConnected()) {
        uint64_t timestamp = getCurrentTimestamp();
        std::string error = "Client not connected";
        if (!lastError_.empty()) {
            error += " - " + lastError_;
        }
        setLastError(error);
        for (const auto& nodeId : nodeIds) {
            results.push_back(ReadResult::createError(nodeId, error, timestamp));
        }
        return results;
    }

    // Use batch reading for multiple nodes to improve performance
    if (nodeIds.size() > 1) {
        return readNodesBatch(nodeIds);
    }

    // Single node - use individual read
    for (const auto& nodeId : nodeIds) {
        results.push_back(readNode(nodeId));
    }

    return results;
}

void OPCUAClient::setStateChangeCallback(StateChangeCallback callback) {
    stateChangeCallback_ = callback;
}

UA_StatusCode OPCUAClient::runIterate(uint16_t timeoutMs) {
    std::lock_guard<std::mutex> lock(clientMutex_);

    if (!initialized_ || !client_) {
        return UA_STATUSCODE_BADINTERNALERROR;
    }

    return UA_Client_run_iterate(client_, timeoutMs);
}

const std::string& OPCUAClient::getEndpoint() const {
    return endpoint_;
}

std::string OPCUAClient::getConnectionInfo() const {
    std::ostringstream oss;
    oss << "Endpoint: " << endpoint_ << ", State: ";

    switch (connectionState_) {
        case ConnectionState::DISCONNECTED: oss << "DISCONNECTED"; break;
        case ConnectionState::CONNECTING: oss << "CONNECTING"; break;
        case ConnectionState::CONNECTED: oss << "CONNECTED"; break;
        case ConnectionState::RECONNECTING: oss << "RECONNECTING"; break;
        case ConnectionState::CONNECTION_ERROR: oss << "CONNECTION_ERROR"; break;
    }

    return oss.str();
}

void OPCUAClient::stateCallback(UA_Client *client,
                               UA_SecureChannelState channelState,
                               UA_SessionState sessionState,
                               UA_StatusCode recoveryStatus) {

    UA_ClientConfig* config = UA_Client_getConfig(client);
    if (!config || !config->clientContext) {
        return;
    }

    OPCUAClient* self = static_cast<OPCUAClient*>(config->clientContext);

    ConnectionState newState = ConnectionState::DISCONNECTED;

    // Handle session states according to open62541 documentation
    switch(sessionState) {
    case UA_SESSIONSTATE_ACTIVATED:
        if (channelState == UA_SECURECHANNELSTATE_OPEN) {
            newState = ConnectionState::CONNECTED;
            spdlog::info("Session activated - connection established");
        }
        break;
    case UA_SESSIONSTATE_CLOSED:
        // This is triggered when server closes abnormally
        newState = ConnectionState::DISCONNECTED;
        spdlog::warn("Session closed - server disconnected abnormally");
        break;
    case UA_SESSIONSTATE_CREATE_REQUESTED:
    case UA_SESSIONSTATE_CREATED:
        newState = ConnectionState::CONNECTING;
        break;
    default:
        // Handle other states based on channel state
        if (channelState == UA_SECURECHANNELSTATE_OPEN) {
            newState = ConnectionState::CONNECTING;
        } else if (channelState == UA_SECURECHANNELSTATE_CLOSED) {
            newState = ConnectionState::DISCONNECTED;
        } else if (recoveryStatus != UA_STATUSCODE_GOOD) {
            newState = ConnectionState::CONNECTION_ERROR;
        }
        break;
    }

    // Log state changes for debugging
    if (self->connectionState_ != newState) {
        spdlog::debug("State callback: channel={}, session={}, recovery={}, newState={}",
                     static_cast<int>(channelState), static_cast<int>(sessionState),
                     self->statusCodeToString(recoveryStatus),
                     static_cast<int>(newState));
    }

    self->updateConnectionState(newState, recoveryStatus);
}

UA_NodeId OPCUAClient::parseNodeId(const std::string& nodeIdStr) {
    UA_NodeId nodeId;
    UA_NodeId_init(&nodeId);

    UA_StatusCode status = UA_NodeId_parse(&nodeId, UA_STRING((char*)nodeIdStr.c_str()));

    if (status != UA_STATUSCODE_GOOD) {
        spdlog::error("Failed to parse NodeId: {} - {}", nodeIdStr, statusCodeToString(status));
        UA_NodeId_init(&nodeId);
    }

    return nodeId;
}

ReadResult OPCUAClient::convertDataValue(const std::string& nodeId, const UA_DataValue& dataValue) {
    uint64_t timestamp = getCurrentTimestamp();

    if (!dataValue.hasValue) {
        return ReadResult::createError(nodeId, "No value available", timestamp);
    }

    if (dataValue.hasStatus && dataValue.status != UA_STATUSCODE_GOOD) {
        return ReadResult::createError(nodeId, statusCodeToString(dataValue.status), timestamp);
    }

    if (dataValue.hasSourceTimestamp) {
        timestamp = dateTimeToTimestamp(dataValue.sourceTimestamp);
    } else if (dataValue.hasServerTimestamp) {
        timestamp = dateTimeToTimestamp(dataValue.serverTimestamp);
    }

    std::string valueStr = variantToString(dataValue.value);

    return ReadResult::createSuccess(nodeId, valueStr, timestamp);
}

std::string OPCUAClient::statusCodeToString(UA_StatusCode statusCode) const {
    const char* statusName = UA_StatusCode_name(statusCode);
    if (statusName) {
        return std::string(statusName);
    }

    std::ostringstream oss;
    oss << "0x" << std::hex << statusCode;
    return oss.str();
}

std::string OPCUAClient::variantToString(const UA_Variant& variant) {
    if (UA_Variant_isEmpty(&variant)) {
        return "";
    }

    if (variant.type == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        bool value = *(UA_Boolean*)variant.data;
        return value ? "true" : "false";
    }
    else if (variant.type == &UA_TYPES[UA_TYPES_INT32]) {
        int32_t value = *(UA_Int32*)variant.data;
        return std::to_string(value);
    }
    else if (variant.type == &UA_TYPES[UA_TYPES_UINT32]) {
        uint32_t value = *(UA_UInt32*)variant.data;
        return std::to_string(value);
    }
    else if (variant.type == &UA_TYPES[UA_TYPES_INT64]) {
        int64_t value = *(UA_Int64*)variant.data;
        return std::to_string(value);
    }
    else if (variant.type == &UA_TYPES[UA_TYPES_UINT64]) {
        uint64_t value = *(UA_UInt64*)variant.data;
        return std::to_string(value);
    }
    else if (variant.type == &UA_TYPES[UA_TYPES_FLOAT]) {
        float value = *(UA_Float*)variant.data;
        return std::to_string(value);
    }
    else if (variant.type == &UA_TYPES[UA_TYPES_DOUBLE]) {
        double value = *(UA_Double*)variant.data;
        return std::to_string(value);
    }
    else if (variant.type == &UA_TYPES[UA_TYPES_STRING]) {
        UA_String* str = (UA_String*)variant.data;
        if (str->data && str->length > 0) {
            return std::string((char*)str->data, str->length);
        }
        return "";
    }
    else if (variant.type == &UA_TYPES[UA_TYPES_DATETIME]) {
        UA_DateTime dateTime = *(UA_DateTime*)variant.data;
        return std::to_string(dateTimeToTimestamp(dateTime));
    }

    return std::string("Unsupported type: ") + variant.type->typeName;
}

uint64_t OPCUAClient::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

uint64_t OPCUAClient::dateTimeToTimestamp(UA_DateTime dateTime) {
    // OPC UA DateTime is 100-nanosecond intervals since January 1, 1601 UTC
    // Unix timestamp is seconds since January 1, 1970 UTC
    const uint64_t epochDiff = 11644473600LL * 10000000LL; // 1601 to 1970 in 100ns intervals

    if (dateTime < epochDiff) {
        return 0;
    }

    uint64_t unixTime100ns = dateTime - epochDiff;
    return unixTime100ns / 10000; // Convert from 100ns to milliseconds
}

bool OPCUAClient::configureClientSecurity() {
    if (!config_) {
        return false;
    }

    switch (appConfig_.securityMode) {
        case 1:
            config_->securityMode = UA_MESSAGESECURITYMODE_NONE;
            break;
        case 2:
            config_->securityMode = UA_MESSAGESECURITYMODE_SIGN;
            break;
        case 3:
            config_->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
            break;
        default:
            config_->securityMode = UA_MESSAGESECURITYMODE_NONE;
            spdlog::warn("Unknown security mode {}, using None", appConfig_.securityMode);
            break;
    }

    if (!appConfig_.applicationUri.empty()) {
        UA_String_clear(&config_->clientDescription.applicationUri);
        config_->clientDescription.applicationUri =
            UA_STRING_ALLOC(appConfig_.applicationUri.c_str());
    }

    spdlog::info("Security configured - Mode: {}, Policy: {}", appConfig_.securityMode, appConfig_.securityPolicy);

    return true;
}

void OPCUAClient::updateConnectionState(ConnectionState newState, UA_StatusCode statusCode) {
    ConnectionState oldState = connectionState_.exchange(newState);

    // Update connection health based on state
    switch (newState) {
        case ConnectionState::CONNECTED:
            connectionHealthy_ = true;
            break;
        case ConnectionState::CONNECTION_ERROR:
        case ConnectionState::DISCONNECTED:
            connectionHealthy_ = false;
            break;
        case ConnectionState::CONNECTING:
        case ConnectionState::RECONNECTING:
            // Keep previous health status during transition states
            break;
    }

    if (oldState != newState) {
        std::string oldStateStr, newStateStr;

        switch (oldState) {
            case ConnectionState::DISCONNECTED: oldStateStr = "DISCONNECTED"; break;
            case ConnectionState::CONNECTING: oldStateStr = "CONNECTING"; break;
            case ConnectionState::CONNECTED: oldStateStr = "CONNECTED"; break;
            case ConnectionState::RECONNECTING: oldStateStr = "RECONNECTING"; break;
            case ConnectionState::CONNECTION_ERROR: oldStateStr = "CONNECTION_ERROR"; break;
        }

        switch (newState) {
            case ConnectionState::DISCONNECTED: newStateStr = "DISCONNECTED"; break;
            case ConnectionState::CONNECTING: newStateStr = "CONNECTING"; break;
            case ConnectionState::CONNECTED: newStateStr = "CONNECTED"; break;
            case ConnectionState::RECONNECTING: newStateStr = "RECONNECTING"; break;
            case ConnectionState::CONNECTION_ERROR: newStateStr = "CONNECTION_ERROR"; break;
        }

        if (statusCode != UA_STATUSCODE_GOOD) {
            std::string errorMsg = "Connection state changed: " + oldStateStr + " -> " + newStateStr +
                                 " (Status: " + statusCodeToString(statusCode) + ")";
            spdlog::info(errorMsg);

            // Update last error for connection issues
            if (newState == ConnectionState::CONNECTION_ERROR) {
                setLastError("Connection error: " + statusCodeToString(statusCode));
            }
        } else {
            spdlog::info("Connection state changed: {} -> {}", oldStateStr, newStateStr);

            // Clear error on successful state transitions
            if (newState == ConnectionState::CONNECTED) {
                setLastError("");
            }
        }

        if (stateChangeCallback_) {
            stateChangeCallback_(newState, statusCode);
        }
    }
}

bool OPCUAClient::validateNodeIdFormat(const std::string& nodeIdStr) {
    if (nodeIdStr.empty()) {
        return false;
    }

    std::regex nodeIdPattern(R"(^ns=\d+;[sig]=.+$)");
    return std::regex_match(nodeIdStr, nodeIdPattern);
}

std::vector<ReadResult> OPCUAClient::readNodesBatch(const std::vector<std::string>& nodeIds) {
    std::lock_guard<std::mutex> lock(clientMutex_);

    if (nodeIds.empty()) {
        return {};
    }

    if (!isConnected()) {
        uint64_t timestamp = getCurrentTimestamp();
        std::string error = "Client not connected";
        if (!lastError_.empty()) {
            error += " - " + lastError_;
        }
        setLastError(error);

        std::vector<ReadResult> results;
        results.reserve(nodeIds.size());
        for (const auto& nodeId : nodeIds) {
            results.push_back(ReadResult::createError(nodeId, error, timestamp));
        }
        return results;
    }

    // Process nodes in batches to respect batch size limits
    std::vector<ReadResult> allResults;
    allResults.reserve(nodeIds.size());

    for (size_t i = 0; i < nodeIds.size(); i += batchSize_) {
        size_t endIdx = std::min(i + batchSize_, nodeIds.size());
        std::vector<std::string> batchNodeIds(nodeIds.begin() + i, nodeIds.begin() + endIdx);

        auto batchResults = performBatchRead(batchNodeIds);
        allResults.insert(allResults.end(), batchResults.begin(), batchResults.end());
    }

    return allResults;
}

std::vector<ReadResult> OPCUAClient::performBatchRead(const std::vector<std::string>& nodeIds) {
    std::vector<ReadResult> results;
    results.reserve(nodeIds.size());
    uint64_t timestamp = getCurrentTimestamp();

    // Separate valid and invalid node IDs
    std::vector<std::string> validNodeIds;
    std::vector<size_t> validIndices;

    for (size_t i = 0; i < nodeIds.size(); ++i) {
        if (validateNodeIdFormat(nodeIds[i])) {
            validNodeIds.push_back(nodeIds[i]);
            validIndices.push_back(i);
        }
    }

    // Initialize results with errors for invalid nodes
    results.resize(nodeIds.size());
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        if (std::find(validIndices.begin(), validIndices.end(), i) == validIndices.end()) {
            results[i] = ReadResult::createError(nodeIds[i], "Invalid NodeId format", timestamp);
        }
    }

    // If no valid nodes, return early
    if (validNodeIds.empty()) {
        return results;
    }

    // Create read request for valid nodes only
    UA_ReadRequest request = createReadRequest(validNodeIds);

    // Perform the batch read operation
    UA_ReadResponse response = UA_Client_Service_read(client_, request);

    // Process response for valid nodes
    std::vector<ReadResult> validResults = processReadResponse(validNodeIds, response);

    // Map valid results back to original positions
    for (size_t i = 0; i < validIndices.size(); ++i) {
        if (i < validResults.size()) {
            results[validIndices[i]] = validResults[i];
        }
    }

    // Cleanup
    UA_ReadRequest_clear(&request);
    UA_ReadResponse_clear(&response);

    return results;
}

UA_ReadRequest OPCUAClient::createReadRequest(const std::vector<std::string>& nodeIds) {
    UA_ReadRequest request;
    UA_ReadRequest_init(&request);

    // Set request header
    request.requestHeader.timestamp = UA_DateTime_now();
    request.requestHeader.timeoutHint = static_cast<UA_UInt32>(readTimeout_.count());

    // Allocate memory for read value IDs
    request.nodesToReadSize = nodeIds.size();
    request.nodesToRead = static_cast<UA_ReadValueId*>(
        UA_Array_new(nodeIds.size(), &UA_TYPES[UA_TYPES_READVALUEID]));

    if (!request.nodesToRead) {
        spdlog::error("Failed to allocate memory for batch read request");
        return request;
    }

    // Initialize each read value ID
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        UA_ReadValueId_init(&request.nodesToRead[i]);

        // Parse and set node ID
        request.nodesToRead[i].nodeId = parseNodeId(nodeIds[i]);

        // Set attribute to read (Value attribute)
        request.nodesToRead[i].attributeId = UA_ATTRIBUTEID_VALUE;

        // No index range or data encoding specified
        request.nodesToRead[i].indexRange = UA_STRING_NULL;
        request.nodesToRead[i].dataEncoding = UA_QUALIFIEDNAME(0, NULL);
    }

    return request;
}

std::vector<ReadResult> OPCUAClient::processReadResponse(const std::vector<std::string>& nodeIds,
                                                        const UA_ReadResponse& response) {
    std::vector<ReadResult> results;
    results.reserve(nodeIds.size());
    uint64_t timestamp = getCurrentTimestamp();

    // Check service result
    if (response.responseHeader.serviceResult != UA_STATUSCODE_GOOD) {
        std::string error = "Batch read service failed: " + statusCodeToString(response.responseHeader.serviceResult);
        setLastError(error);
        spdlog::error(error);

        for (const auto& nodeId : nodeIds) {
            results.push_back(ReadResult::createError(nodeId, error, timestamp));
        }
        return results;
    }

    // Check if we have results for all requested nodes
    if (response.resultsSize != nodeIds.size()) {
        std::string error = "Batch read returned unexpected number of results: expected " +
                           std::to_string(nodeIds.size()) + ", got " + std::to_string(response.resultsSize);
        setLastError(error);
        spdlog::error(error);

        for (const auto& nodeId : nodeIds) {
            results.push_back(ReadResult::createError(nodeId, error, timestamp));
        }
        return results;
    }

    // Process each result
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        const UA_DataValue& dataValue = response.results[i];
        results.push_back(convertDataValue(nodeIds[i], dataValue));
    }

    return results;
}

std::string OPCUAClient::getLastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

void OPCUAClient::setReadTimeout(std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(clientMutex_);
    readTimeout_ = timeout;

    // Update client configuration if initialized
    if (config_) {
        config_->timeout = static_cast<UA_UInt32>(timeout.count());
    }

    spdlog::info("OPC UA read timeout set to {}ms", timeout.count());
}

void OPCUAClient::setRetryCount(int retries) {
    std::lock_guard<std::mutex> lock(clientMutex_);
    retryCount_ = retries;
    spdlog::info("OPC UA retry count set to {}", retries);
}

void OPCUAClient::setConnectionTimeout(std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(clientMutex_);
    connectionTimeout_ = timeout;

    // Update client configuration if initialized
    if (config_) {
        config_->timeout = static_cast<UA_UInt32>(timeout.count());
    }

    spdlog::info("OPC UA connection timeout set to {}ms", timeout.count());
}

bool OPCUAClient::isConnectionHealthy() const {
    // First check basic connection state
    if (!connectionHealthy_.load() || connectionState_ != ConnectionState::CONNECTED) {
        return false;
    }
    
    // Perform a lightweight health check by reading the server status
    std::lock_guard<std::mutex> lock(clientMutex_);
    
    if (!client_) {
        return false;
    }
    
    // Try to read the server state node (a standard OPC UA node that should always be available)
    UA_NodeId serverStateNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STATE);
    UA_Variant value;
    UA_Variant_init(&value);
    
    UA_StatusCode status = UA_Client_readValueAttribute(client_, serverStateNodeId, &value);
    UA_Variant_clear(&value);
    
    if (status != UA_STATUSCODE_GOOD) {
        // Connection appears to be broken, update our state
        spdlog::warn("Connection health check failed: {}", statusCodeToString(status));
        
        // Update connection state to reflect the actual situation
        const_cast<OPCUAClient*>(this)->connectionHealthy_.store(false);
        const_cast<OPCUAClient*>(this)->updateConnectionState(ConnectionState::CONNECTION_ERROR, status);
        
        return false;
    }
    
    return true;
}

bool OPCUAClient::performHealthCheck() const {
    // Quick health check without modifying state
    if (!connectionHealthy_.load() || connectionState_ != ConnectionState::CONNECTED) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(clientMutex_);
    
    if (!client_) {
        return false;
    }
    
    // Try to read the server state node (a standard OPC UA node that should always be available)
    UA_NodeId serverStateNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERSTATUS_STATE);
    UA_Variant value;
    UA_Variant_init(&value);
    
    UA_StatusCode status = UA_Client_readValueAttribute(client_, serverStateNodeId, &value);
    UA_Variant_clear(&value);
    
    return status == UA_STATUSCODE_GOOD;
}

std::chrono::steady_clock::time_point OPCUAClient::getLastConnectionAttempt() const {
    std::lock_guard<std::mutex> lock(clientMutex_);
    return lastConnectionAttempt_;
}

std::chrono::milliseconds OPCUAClient::getTimeSinceLastAttempt() const {
    std::lock_guard<std::mutex> lock(clientMutex_);
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - lastConnectionAttempt_);
}

void OPCUAClient::setLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = error;
}

} // namespace opcua2http
