#pragma once

#include <string>
#include <exception>
#include <functional>

namespace opcua2http {

/**
 * @brief Centralized error handling and recovery system
 */
class ErrorHandler {
public:
    enum class ErrorType {
        CONNECTION_LOST,
        SUBSCRIPTION_FAILED,
        CACHE_ERROR,
        HTTP_ERROR,
        CONFIGURATION_ERROR,
        INITIALIZATION_ERROR,
        UNKNOWN_ERROR
    };
    
    /**
     * @brief Error recovery callback function type
     */
    using RecoveryCallback = std::function<bool()>;
    
    /**
     * @brief Handle an error with optional recovery
     * @param type Type of error
     * @param details Error details
     * @param recovery Optional recovery function
     * @return true if error was handled/recovered, false otherwise
     */
    static bool handleError(ErrorType type, const std::string& details, 
                           RecoveryCallback recovery = nullptr);
    
    /**
     * @brief Handle an exception
     * @param e Exception to handle
     * @param context Context where exception occurred
     * @param recovery Optional recovery function
     * @return true if exception was handled/recovered, false otherwise
     */
    static bool handleException(const std::exception& e, const std::string& context,
                               RecoveryCallback recovery = nullptr);
    
    /**
     * @brief Get error type as string
     * @param type Error type
     * @return String representation of error type
     */
    static std::string errorTypeToString(ErrorType type);
    
    /**
     * @brief Execute a function with error handling
     * @param func Function to execute
     * @param context Context description
     * @param recovery Optional recovery function
     * @return true if function executed successfully or was recovered, false otherwise
     */
    template<typename Func>
    static bool executeWithErrorHandling(Func&& func, const std::string& context,
                                        RecoveryCallback recovery = nullptr) {
        try {
            func();
            return true;
        } catch (const std::exception& e) {
            return handleException(e, context, recovery);
        } catch (...) {
            return handleError(ErrorType::UNKNOWN_ERROR, 
                             "Unknown exception in " + context, recovery);
        }
    }
    
private:
    /**
     * @brief Log error details
     * @param type Error type
     * @param details Error details
     */
    static void logError(ErrorType type, const std::string& details);
    
    /**
     * @brief Attempt recovery
     * @param recovery Recovery function
     * @return true if recovery successful, false otherwise
     */
    static bool attemptRecovery(RecoveryCallback recovery);
};

} // namespace opcua2http