#include "core/ErrorHandler.h"
#include <spdlog/spdlog.h>

namespace opcua2http {

bool ErrorHandler::handleError(ErrorType type, const std::string& details, 
                              RecoveryCallback recovery) {
    logError(type, details);
    
    if (recovery) {
        return attemptRecovery(recovery);
    }
    
    return false;
}

bool ErrorHandler::handleException(const std::exception& e, const std::string& context,
                                  RecoveryCallback recovery) {
    std::string details = "Exception in " + context + ": " + e.what();
    return handleError(ErrorType::UNKNOWN_ERROR, details, recovery);
}

std::string ErrorHandler::errorTypeToString(ErrorType type) {
    switch (type) {
        case ErrorType::CONNECTION_LOST:
            return "CONNECTION_LOST";
        case ErrorType::SUBSCRIPTION_FAILED:
            return "SUBSCRIPTION_FAILED";
        case ErrorType::CACHE_ERROR:
            return "CACHE_ERROR";
        case ErrorType::HTTP_ERROR:
            return "HTTP_ERROR";
        case ErrorType::CONFIGURATION_ERROR:
            return "CONFIGURATION_ERROR";
        case ErrorType::INITIALIZATION_ERROR:
            return "INITIALIZATION_ERROR";
        case ErrorType::UNKNOWN_ERROR:
            return "UNKNOWN_ERROR";
        default:
            return "UNDEFINED_ERROR";
    }
}

void ErrorHandler::logError(ErrorType type, const std::string& details) {
    std::string errorMsg = "[" + errorTypeToString(type) + "] " + details;
    spdlog::error(errorMsg);
}

bool ErrorHandler::attemptRecovery(RecoveryCallback recovery) {
    try {
        spdlog::info("Attempting error recovery...");
        bool success = recovery();
        
        if (success) {
            spdlog::info("Error recovery successful");
        } else {
            spdlog::warn("Error recovery failed");
        }
        
        return success;
        
    } catch (const std::exception& e) {
        spdlog::error("Exception during error recovery: {}", e.what());
        return false;
    } catch (...) {
        spdlog::error("Unknown exception during error recovery");
        return false;
    }
}

} // namespace opcua2http