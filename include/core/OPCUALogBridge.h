#pragma once

#include <open62541/types.h>
#include <open62541/client.h>
#include <string>

namespace opcua2http {

/**
 * @brief Bridge class to integrate OPC UA logging with spdlog
 * 
 * This class provides a unified logging interface that captures OPC UA
 * library log messages and forwards them to our spdlog-based logging system.
 */
class OPCUALogBridge {
public:
    /**
     * @brief Create a UA_Logger that integrates with our logging system
     * @return Configured UA_Logger instance
     */
    static UA_Logger createLogger();
    
    /**
     * @brief Set the minimum log level for OPC UA messages
     * @param level Minimum log level to capture
     */
    static void setLogLevel(UA_LogLevel level);
    
private:
    // Static minimum log level
    static UA_LogLevel minLogLevel_;
    
    /**
     * @brief Callback function for OPC UA log messages
     * @param logContext Context pointer (unused)
     * @param level Log level
     * @param category Log category
     * @param msg Message format string
     * @param args Variable arguments
     */
    static void logCallback(void* logContext, UA_LogLevel level, 
                           UA_LogCategory category, const char* msg, va_list args);
    
    /**
     * @brief Callback function for logger cleanup
     * @param logger Logger to clean up
     */
    static void clearCallback(UA_Logger* logger);
    
    /**
     * @brief Convert OPC UA log level to spdlog level
     * @param level OPC UA log level
     * @return Corresponding spdlog level
     */
    static int convertLogLevel(UA_LogLevel level);
    
    /**
     * @brief Get string representation of OPC UA log category
     * @param category OPC UA log category
     * @return Category name string
     */
    static const char* getCategoryName(UA_LogCategory category);
};

} // namespace opcua2http