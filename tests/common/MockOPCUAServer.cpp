#include "MockOPCUAServer.h"
#include <chrono>

namespace opcua2http {
namespace test {

MockOPCUAServer::MockOPCUAServer(uint16_t port, const std::string& namespaceName)
    : port_(port)
    , namespaceName_(namespaceName)
    , server_(nullptr)
    , running_(false)
    , serverReady_(false)
    , testNamespaceIndex_(0)
    , startupTimeoutMs_(1000)
    , verboseLogging_(true) {
}

MockOPCUAServer::~MockOPCUAServer() {
    stop();
}

bool MockOPCUAServer::start() {
    if (running_) {
        logMessage("Server already running");
        return true;
    }
    
    server_ = UA_Server_new();
    if (!server_) {
        logMessage("Failed to create UA_Server");
        return false;
    }
    
    // Configure server
    UA_ServerConfig* config = UA_Server_getConfig(server_);
    UA_StatusCode status = UA_ServerConfig_setMinimal(config, port_, nullptr);
    if (status != UA_STATUSCODE_GOOD) {
        logMessage("Failed to set minimal server config: " + std::string(UA_StatusCode_name(status)));
        UA_Server_delete(server_);
        server_ = nullptr;
        return false;
    }
    
    // Add test namespace
    testNamespaceIndex_ = UA_Server_addNamespace(server_, namespaceName_.c_str());
    logMessage("Added namespace '" + namespaceName_ + "' with index: " + std::to_string(testNamespaceIndex_));
    
    // Add any pre-configured test variables
    for (const auto& testVar : testVariables_) {
        addTestVariableInternal(testVar.nodeId, testVar.name, testVar.value);
    }
    
    // Start server in separate thread
    running_ = true;
    serverReady_ = false;
    
    serverThread_ = std::thread(&MockOPCUAServer::serverThreadFunction, this);
    
    // Wait for server to be ready
    return waitForServerReady();
}

void MockOPCUAServer::stop() {
    if (running_) {
        running_ = false;
        
        // Give server thread time to notice the stop signal
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
    }
    
    if (server_) {
        UA_Server_delete(server_);
        server_ = nullptr;
    }
    
    serverReady_ = false;
    logMessage("Mock OPC UA server stopped");
}

std::string MockOPCUAServer::getEndpoint() const {
    return "opc.tcp://localhost:" + std::to_string(port_);
}

bool MockOPCUAServer::addTestVariable(UA_UInt32 nodeId, const std::string& name, const UA_Variant& value) {
    if (running_) {
        // Server is running, add variable directly
        return addTestVariableInternal(nodeId, name, value);
    } else {
        // Server not running, store for later addition
        testVariables_.emplace_back(nodeId, name, value);
        return true;
    }
}

void MockOPCUAServer::addStandardTestVariables() {
    // Integer variable
    UA_Variant intValue = TestValueFactory::createInt32(42);
    addTestVariable(1001, "TestInt", intValue);
    UA_Variant_clear(&intValue);
    
    // String variable
    UA_Variant stringValue = TestValueFactory::createString("Hello World");
    addTestVariable(1002, "TestString", stringValue);
    UA_Variant_clear(&stringValue);
    
    // Boolean variable
    UA_Variant boolValue = TestValueFactory::createBoolean(true);
    addTestVariable(1003, "TestBool", boolValue);
    UA_Variant_clear(&boolValue);
}

void MockOPCUAServer::updateTestVariable(UA_UInt32 nodeId, const UA_Variant& newValue) {
    if (!server_) return;
    
    UA_NodeId testNodeId = UA_NODEID_NUMERIC(testNamespaceIndex_, nodeId);
    UA_StatusCode status = UA_Server_writeValue(server_, testNodeId, newValue);
    
    if (status == UA_STATUSCODE_GOOD) {
        logMessage("Updated variable ns=" + std::to_string(testNamespaceIndex_) + ";i=" + std::to_string(nodeId));
    } else {
        logMessage("Failed to update variable: " + std::string(UA_StatusCode_name(status)));
    }
}

std::string MockOPCUAServer::getNodeIdString(UA_UInt32 nodeId) const {
    return "ns=" + std::to_string(testNamespaceIndex_) + ";i=" + std::to_string(nodeId);
}

void MockOPCUAServer::serverThreadFunction() {
    // Start the server
    UA_StatusCode status = UA_Server_run_startup(server_);
    if (status != UA_STATUSCODE_GOOD) {
        logMessage("Failed to start server: " + std::string(UA_StatusCode_name(status)));
        running_ = false;
        return;
    }
    
    serverReady_ = true;
    logMessage("Mock OPC UA server started on port " + std::to_string(port_));
    
    // Run server loop
    while (running_) {
        UA_Server_run_iterate(server_, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Shutdown server
    UA_Server_run_shutdown(server_);
}

bool MockOPCUAServer::waitForServerReady() {
    auto startTime = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(startupTimeoutMs_);
    
    while (!serverReady_ && running_) {
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (elapsed > timeout) {
            logMessage("Server failed to start within timeout (" + std::to_string(startupTimeoutMs_) + "ms)");
            stop();
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    if (!serverReady_) {
        return false;
    }
    
    // Additional wait to ensure server is fully ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return true;
}

bool MockOPCUAServer::addTestVariableInternal(UA_UInt32 nodeId, const std::string& name, const UA_Variant& value) {
    if (!server_) return false;
    
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = UA_LOCALIZEDTEXT(const_cast<char*>("en-US"), const_cast<char*>(name.c_str()));
    
    // Copy the value
    UA_Variant_copy(&value, &attr.value);
    
    // Set appropriate data type and attributes based on variant type
    if (value.type == &UA_TYPES[UA_TYPES_INT32]) {
        attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
    } else if (value.type == &UA_TYPES[UA_TYPES_STRING]) {
        attr.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
    } else if (value.type == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        attr.dataType = UA_TYPES[UA_TYPES_BOOLEAN].typeId;
    } else if (value.type == &UA_TYPES[UA_TYPES_DOUBLE]) {
        attr.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
    } else if (value.type == &UA_TYPES[UA_TYPES_FLOAT]) {
        attr.dataType = UA_TYPES[UA_TYPES_FLOAT].typeId;
    }
    
    attr.valueRank = UA_VALUERANK_SCALAR;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    attr.userAccessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
    
    UA_NodeId newNodeId = UA_NODEID_NUMERIC(testNamespaceIndex_, nodeId);
    UA_NodeId parentNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId parentReferenceNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_NodeId variableType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
    UA_QualifiedName browseName = UA_QUALIFIEDNAME(testNamespaceIndex_, const_cast<char*>(name.c_str()));
    
    UA_StatusCode status = UA_Server_addVariableNode(server_, newNodeId, parentNodeId,
                              parentReferenceNodeId, browseName,
                              variableType, attr, nullptr, nullptr);
    
    if (status != UA_STATUSCODE_GOOD) {
        logMessage("Failed to add variable '" + name + "': " + std::string(UA_StatusCode_name(status)));
        return false;
    } else {
        logMessage("Added variable '" + name + "': " + getNodeIdString(nodeId));
        return true;
    }
}

void MockOPCUAServer::logMessage(const std::string& message) const {
    if (verboseLogging_) {
        std::cout << "[MockOPCUAServer] " << message << std::endl;
    }
}

// TestValueFactory implementations
UA_Variant TestValueFactory::createInt32(UA_Int32 value) {
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalarCopy(&variant, &value, &UA_TYPES[UA_TYPES_INT32]);
    return variant;
}

UA_Variant TestValueFactory::createString(const std::string& value) {
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_String uaString = UA_STRING_ALLOC(value.c_str());
    UA_Variant_setScalarCopy(&variant, &uaString, &UA_TYPES[UA_TYPES_STRING]);
    UA_String_clear(&uaString);
    return variant;
}

UA_Variant TestValueFactory::createBoolean(UA_Boolean value) {
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalarCopy(&variant, &value, &UA_TYPES[UA_TYPES_BOOLEAN]);
    return variant;
}

UA_Variant TestValueFactory::createDouble(UA_Double value) {
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalarCopy(&variant, &value, &UA_TYPES[UA_TYPES_DOUBLE]);
    return variant;
}

UA_Variant TestValueFactory::createFloat(UA_Float value) {
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalarCopy(&variant, &value, &UA_TYPES[UA_TYPES_FLOAT]);
    return variant;
}

} // namespace test
} // namespace opcua2http