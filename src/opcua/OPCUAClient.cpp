#include "opcua/OPCUAClient.h"
#include <iostream>
#include <sstream>
#include <regex>
#include <chrono>
#include <cstring>

namespace opcua2http {

OPCUAClient::OPCUAClient() 
    : client_(nullptr)
    , config_(nullptr)
    , connectionState_(ConnectionState::DISCONNECTED)
    , initialized_(false)
    , stateChangeCallback_(nullptr) {
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
        std::cerr << "OPCUAClient already initialized" << std::endl;
        return false;
    }
    
    appConfig_ = config;
    endpoint_ = config.opcEndpoint;
    
    if (endpoint_.empty()) {
        std::cerr << "OPC UA endpoint is empty" << std::endl;
        return false;
    }
    
    client_ = UA_Client_new();
    if (!client_) {
        std::cerr << "Failed to create UA_Client" << std::endl;
        return false;
    }
    
    config_ = UA_Client_getConfig(client_);
    if (!config_) {
        std::cerr << "Failed to get client configuration" << std::endl;
        UA_Client_delete(client_);
        client_ = nullptr;
        return false;
    }
    
    UA_StatusCode status = UA_ClientConfig_setDefault(config_);
    if (status != UA_STATUSCODE_GOOD) {
        std::cerr << "Failed to set default client configuration: " 
                  << statusCodeToString(status) << std::endl;
        UA_Client_delete(client_);
        client_ = nullptr;
        return false;
    }
    
    if (!configureClientSecurity()) {
        std::cerr << "Failed to configure client security" << std::endl;
        UA_Client_delete(client_);
        client_ = nullptr;
        return false;
    }
    
    config_->stateCallback = stateCallback;
    config_->clientContext = this;
    config_->timeout = 5000;
    
    initialized_ = true;
    updateConnectionState(ConnectionState::DISCONNECTED);
    
    std::cout << "OPCUAClient initialized successfully for endpoint: " << endpoint_ << std::endl;
    return true;
}

bool OPCUAClient::connect() {
    std::lock_guard<std::mutex> lock(clientMutex_);
    
    if (!initialized_) {
        std::cerr << "Client not initialized" << std::endl;
        return false;
    }
    
    if (connectionState_ == ConnectionState::CONNECTED) {
        return true;
    }
    
    updateConnectionState(ConnectionState::CONNECTING);
    lastConnectionAttempt_ = std::chrono::steady_clock::now();
    
    std::cout << "Connecting to OPC UA server: " << endpoint_ << std::endl;
    
    UA_StatusCode status = UA_Client_connect(client_, endpoint_.c_str());
    
    if (status == UA_STATUSCODE_GOOD) {
        updateConnectionState(ConnectionState::CONNECTED);
        std::cout << "Successfully connected to OPC UA server" << std::endl;
        return true;
    } else {
        updateConnectionState(ConnectionState::CONNECTION_ERROR, status);
        std::cerr << "Failed to connect to OPC UA server: " 
                  << statusCodeToString(status) << std::endl;
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
        
        std::cout << "Disconnecting from OPC UA server" << std::endl;
        UA_Client_disconnect(client_);
        updateConnectionState(ConnectionState::DISCONNECTED);
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
        return ReadResult::createError(nodeId, "Client not connected", getCurrentTimestamp());
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
        for (const auto& nodeId : nodeIds) {
            results.push_back(ReadResult::createError(nodeId, "Client not connected", timestamp));
        }
        return results;
    }
    
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
    
    if (channelState == UA_SECURECHANNELSTATE_OPEN && 
        sessionState == UA_SESSIONSTATE_ACTIVATED) {
        newState = ConnectionState::CONNECTED;
    } else if (channelState == UA_SECURECHANNELSTATE_OPEN) {
        newState = ConnectionState::CONNECTING;
    } else if (recoveryStatus != UA_STATUSCODE_GOOD) {
        newState = ConnectionState::CONNECTION_ERROR;
    }
    
    self->updateConnectionState(newState, recoveryStatus);
}

UA_NodeId OPCUAClient::parseNodeId(const std::string& nodeIdStr) {
    UA_NodeId nodeId;
    UA_NodeId_init(&nodeId);
    
    UA_StatusCode status = UA_NodeId_parse(&nodeId, UA_STRING((char*)nodeIdStr.c_str()));
    
    if (status != UA_STATUSCODE_GOOD) {
        std::cerr << "Failed to parse NodeId: " << nodeIdStr 
                  << " - " << statusCodeToString(status) << std::endl;
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

std::string OPCUAClient::statusCodeToString(UA_StatusCode statusCode) {
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
            std::cout << "Unknown security mode " << appConfig_.securityMode 
                      << ", using None" << std::endl;
            break;
    }
    
    if (!appConfig_.applicationUri.empty()) {
        UA_String_clear(&config_->clientDescription.applicationUri);
        config_->clientDescription.applicationUri = 
            UA_STRING_ALLOC(appConfig_.applicationUri.c_str());
    }
    
    std::cout << "Security configured - Mode: " << appConfig_.securityMode 
              << ", Policy: " << appConfig_.securityPolicy << std::endl;
    
    return true;
}

void OPCUAClient::updateConnectionState(ConnectionState newState, UA_StatusCode statusCode) {
    ConnectionState oldState = connectionState_.exchange(newState);
    
    if (oldState != newState) {
        std::cout << "Connection state changed: ";
        
        switch (oldState) {
            case ConnectionState::DISCONNECTED: std::cout << "DISCONNECTED"; break;
            case ConnectionState::CONNECTING: std::cout << "CONNECTING"; break;
            case ConnectionState::CONNECTED: std::cout << "CONNECTED"; break;
            case ConnectionState::RECONNECTING: std::cout << "RECONNECTING"; break;
            case ConnectionState::CONNECTION_ERROR: std::cout << "CONNECTION_ERROR"; break;
        }
        
        std::cout << " -> ";
        
        switch (newState) {
            case ConnectionState::DISCONNECTED: std::cout << "DISCONNECTED"; break;
            case ConnectionState::CONNECTING: std::cout << "CONNECTING"; break;
            case ConnectionState::CONNECTED: std::cout << "CONNECTED"; break;
            case ConnectionState::RECONNECTING: std::cout << "RECONNECTING"; break;
            case ConnectionState::CONNECTION_ERROR: std::cout << "CONNECTION_ERROR"; break;
        }
        
        if (statusCode != UA_STATUSCODE_GOOD) {
            std::cout << " (Status: " << statusCodeToString(statusCode) << ")";
        }
        
        std::cout << std::endl;
        
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

} // namespace opcua2http