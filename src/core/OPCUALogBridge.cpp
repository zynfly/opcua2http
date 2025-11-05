#include "core/OPCUALogBridge.h"
#include <spdlog/spdlog.h>
#include <cstdio>
#include <cstdarg>

namespace opcua2http {

// Initialize static member
UA_LogLevel OPCUALogBridge::minLogLevel_ = UA_LOGLEVEL_INFO;

UA_Logger OPCUALogBridge::createLogger() {
    UA_Logger logger;
    logger.log = logCallback;
    logger.context = nullptr;
    logger.clear = clearCallback;
    return logger;
}

void OPCUALogBridge::setLogLevel(UA_LogLevel level) {
    minLogLevel_ = level;
}

void OPCUALogBridge::logCallback(void* logContext, UA_LogLevel level,
                                UA_LogCategory category, const char* msg, va_list args) {
    (void)logContext; // Suppress unused parameter warning

    // Skip if below minimum level
    if (level < minLogLevel_) {
        return;
    }

    // Format the message with variable arguments
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), msg, args);

    // Create formatted message with category
    std::string formattedMsg = "[OPC UA][";
    formattedMsg += getCategoryName(category);
    formattedMsg += "] ";
    formattedMsg += buffer;

    // Convert and log through spdlog
    switch (level) {
        case UA_LOGLEVEL_TRACE:
            spdlog::debug(formattedMsg);
            break;
        case UA_LOGLEVEL_DEBUG:
            spdlog::debug(formattedMsg);
            break;
        case UA_LOGLEVEL_INFO:
            spdlog::info(formattedMsg);
            break;
        case UA_LOGLEVEL_WARNING:
            spdlog::warn(formattedMsg);
            break;
        case UA_LOGLEVEL_ERROR:
            spdlog::error(formattedMsg);
            break;
        case UA_LOGLEVEL_FATAL:
            spdlog::critical(formattedMsg);
            break;
        default:
            spdlog::info(formattedMsg);
            break;
    }
}

void OPCUALogBridge::clearCallback(struct UA_Logger* logger) {
    // No cleanup needed for spdlog
    (void)logger; // Suppress unused parameter warning
}

int OPCUALogBridge::convertLogLevel(UA_LogLevel level) {
    switch (level) {
        case UA_LOGLEVEL_TRACE:
            return 0; // TRACE/DEBUG
        case UA_LOGLEVEL_DEBUG:
            return 0; // DEBUG
        case UA_LOGLEVEL_INFO:
            return 1; // INFO
        case UA_LOGLEVEL_WARNING:
            return 2; // WARN
        case UA_LOGLEVEL_ERROR:
            return 3; // ERROR
        case UA_LOGLEVEL_FATAL:
            return 4; // CRITICAL
        default:
            return 1; // INFO
    }
}

const char* OPCUALogBridge::getCategoryName(UA_LogCategory category) {
    switch (category) {
        case UA_LOGCATEGORY_NETWORK:
            return "Network";
        case UA_LOGCATEGORY_SECURECHANNEL:
            return "SecureChannel";
        case UA_LOGCATEGORY_SESSION:
            return "Session";
        case UA_LOGCATEGORY_SERVER:
            return "Server";
        case UA_LOGCATEGORY_CLIENT:
            return "Client";
        case UA_LOGCATEGORY_USERLAND:
            return "Userland";
        case UA_LOGCATEGORY_SECURITYPOLICY:
            return "SecurityPolicy";
        default:
            return "Unknown";
    }
}

} // namespace opcua2http
