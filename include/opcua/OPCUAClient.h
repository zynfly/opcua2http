#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>
#include <chrono>

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>

#include "config/Configuration.h"
#include "core/ReadResult.h"

namespace opcua2http {

class OPCUAClient {
public:
    enum class ConnectionState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        RECONNECTING,
        CONNECTION_ERROR
    };

    using StateChangeCallback = std::function<void(ConnectionState state, UA_StatusCode statusCode)>;

    OPCUAClient();
    ~OPCUAClient();

    OPCUAClient(const OPCUAClient&) = delete;
    OPCUAClient& operator=(const OPCUAClient&) = delete;

    bool initialize(const Configuration& config);
    bool connect();
    void disconnect();
    bool isConnected() const;
    ConnectionState getConnectionState() const;
    UA_Client* getClient() const;
    ReadResult readNode(const std::string& nodeId);
    std::vector<ReadResult> readNodes(const std::vector<std::string>& nodeIds);

    // NEW: Batch reading capabilities for efficient multi-node reads
    std::vector<ReadResult> readNodesBatch(const std::vector<std::string>& nodeIds);

    // NEW: Enhanced connection state management for cache fallback
    std::string getLastError() const;

    // NEW: Connection timeout and retry configuration
    void setReadTimeout(std::chrono::milliseconds timeout);
    void setRetryCount(int retries);
    void setConnectionTimeout(std::chrono::milliseconds timeout);

    // NEW: Enhanced connection management
    bool isConnectionHealthy() const;
    std::chrono::steady_clock::time_point getLastConnectionAttempt() const;
    std::chrono::milliseconds getTimeSinceLastAttempt() const;

    void setStateChangeCallback(StateChangeCallback callback);
    UA_StatusCode runIterate(uint16_t timeoutMs = 0);
    const std::string& getEndpoint() const;
    std::string getConnectionInfo() const;

private:
    UA_Client* client_;
    UA_ClientConfig* config_;
    Configuration appConfig_;
    std::string endpoint_;
    std::atomic<ConnectionState> connectionState_;
    std::atomic<bool> initialized_;
    mutable std::mutex clientMutex_;
    StateChangeCallback stateChangeCallback_;
    std::chrono::steady_clock::time_point lastConnectionAttempt_;

    // NEW: Enhanced connection and error management
    std::string lastError_;
    std::chrono::milliseconds readTimeout_;
    std::chrono::milliseconds connectionTimeout_;
    int retryCount_;
    size_t batchSize_;
    std::atomic<bool> connectionHealthy_;
    mutable std::mutex errorMutex_;

    static void stateCallback(UA_Client *client,
                            UA_SecureChannelState channelState,
                            UA_SessionState sessionState,
                            UA_StatusCode recoveryStatus);
    UA_NodeId parseNodeId(const std::string& nodeIdStr);
    ReadResult convertDataValue(const std::string& nodeId, const UA_DataValue& dataValue);
    std::string statusCodeToString(UA_StatusCode statusCode);
    std::string variantToString(const UA_Variant& variant);
    uint64_t getCurrentTimestamp();
    uint64_t dateTimeToTimestamp(UA_DateTime dateTime);
    bool configureClientSecurity();
    void updateConnectionState(ConnectionState newState, UA_StatusCode statusCode = UA_STATUSCODE_GOOD);
    bool validateNodeIdFormat(const std::string& nodeIdStr);

    // NEW: Batch reading helper methods
    std::vector<ReadResult> performBatchRead(const std::vector<std::string>& nodeIds);
    UA_ReadRequest createReadRequest(const std::vector<std::string>& nodeIds);
    std::vector<ReadResult> processReadResponse(const std::vector<std::string>& nodeIds,
                                               const UA_ReadResponse& response);
    void setLastError(const std::string& error);
};

} // namespace opcua2http
