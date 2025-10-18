#include <gtest/gtest.h>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include "core/ErrorHandler.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>

using namespace opcua2http;

class ErrorHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up spdlog to capture output for testing
        auto cout_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(cout_stream);
        auto cerr_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(cerr_stream);
        
        // Create logger with both sinks
        auto logger = std::make_shared<spdlog::logger>("test", spdlog::sinks_init_list{cout_sink, cerr_sink});
        logger->set_level(spdlog::level::debug);
        spdlog::set_default_logger(logger);
        
        recovery_called = false;
        recovery_success = false;
    }
    
    void TearDown() override {
        // Reset spdlog to default
        spdlog::set_default_logger(nullptr);
    }
    
    std::stringstream cout_stream;
    std::stringstream cerr_stream;
    
    bool recovery_called;
    bool recovery_success;
    
    // Helper recovery function
    ErrorHandler::RecoveryCallback createRecoveryCallback(bool success) {
        return [this, success]() -> bool {
            recovery_called = true;
            recovery_success = success;
            return success;
        };
    }
};

TEST_F(ErrorHandlerTest, HandleError_WithoutRecovery_LogsErrorAndReturnsFalse) {
    bool result = ErrorHandler::handleError(
        ErrorHandler::ErrorType::CONNECTION_LOST,
        "Test connection error"
    );
    
    EXPECT_FALSE(result);
    
    std::string output = cout_stream.str() + cerr_stream.str();
    EXPECT_TRUE(output.find("CONNECTION_LOST") != std::string::npos);
    EXPECT_TRUE(output.find("Test connection error") != std::string::npos);
}

TEST_F(ErrorHandlerTest, HandleError_WithSuccessfulRecovery_LogsAndReturnsTrue) {
    bool result = ErrorHandler::handleError(
        ErrorHandler::ErrorType::SUBSCRIPTION_FAILED,
        "Test subscription error",
        createRecoveryCallback(true)
    );
    
    EXPECT_TRUE(result);
    EXPECT_TRUE(recovery_called);
    
    std::string output = cout_stream.str() + cerr_stream.str();
    EXPECT_TRUE(output.find("SUBSCRIPTION_FAILED") != std::string::npos);
    EXPECT_TRUE(output.find("Test subscription error") != std::string::npos);
    EXPECT_TRUE(output.find("Attempting error recovery") != std::string::npos);
    EXPECT_TRUE(output.find("Error recovery successful") != std::string::npos);
}

TEST_F(ErrorHandlerTest, HandleError_WithFailedRecovery_LogsAndReturnsFalse) {
    bool result = ErrorHandler::handleError(
        ErrorHandler::ErrorType::CACHE_ERROR,
        "Test cache error",
        createRecoveryCallback(false)
    );
    
    EXPECT_FALSE(result);
    EXPECT_TRUE(recovery_called);
    
    std::string output = cout_stream.str() + cerr_stream.str();
    EXPECT_TRUE(output.find("CACHE_ERROR") != std::string::npos);
    EXPECT_TRUE(output.find("Attempting error recovery") != std::string::npos);
    EXPECT_TRUE(output.find("Error recovery failed") != std::string::npos);
}

TEST_F(ErrorHandlerTest, HandleException_StandardException_LogsAndHandlesCorrectly) {
    std::runtime_error test_exception("Test runtime error");
    
    bool result = ErrorHandler::handleException(
        test_exception,
        "test context"
    );
    
    EXPECT_FALSE(result);
    
    std::string output = cout_stream.str() + cerr_stream.str();
    EXPECT_TRUE(output.find("UNKNOWN_ERROR") != std::string::npos);
    EXPECT_TRUE(output.find("Exception in test context") != std::string::npos);
    EXPECT_TRUE(output.find("Test runtime error") != std::string::npos);
}

TEST_F(ErrorHandlerTest, HandleException_WithRecovery_AttemptsRecovery) {
    std::logic_error test_exception("Test logic error");
    
    bool result = ErrorHandler::handleException(
        test_exception,
        "test context",
        createRecoveryCallback(true)
    );
    
    EXPECT_TRUE(result);
    EXPECT_TRUE(recovery_called);
    
    std::string output = cout_stream.str() + cerr_stream.str();
    EXPECT_TRUE(output.find("Test logic error") != std::string::npos);
}

TEST_F(ErrorHandlerTest, ErrorTypeToString_AllTypes_ReturnsCorrectStrings) {
    EXPECT_EQ("CONNECTION_LOST", 
              ErrorHandler::errorTypeToString(ErrorHandler::ErrorType::CONNECTION_LOST));
    EXPECT_EQ("SUBSCRIPTION_FAILED", 
              ErrorHandler::errorTypeToString(ErrorHandler::ErrorType::SUBSCRIPTION_FAILED));
    EXPECT_EQ("CACHE_ERROR", 
              ErrorHandler::errorTypeToString(ErrorHandler::ErrorType::CACHE_ERROR));
    EXPECT_EQ("HTTP_ERROR", 
              ErrorHandler::errorTypeToString(ErrorHandler::ErrorType::HTTP_ERROR));
    EXPECT_EQ("CONFIGURATION_ERROR", 
              ErrorHandler::errorTypeToString(ErrorHandler::ErrorType::CONFIGURATION_ERROR));
    EXPECT_EQ("INITIALIZATION_ERROR", 
              ErrorHandler::errorTypeToString(ErrorHandler::ErrorType::INITIALIZATION_ERROR));
    EXPECT_EQ("UNKNOWN_ERROR", 
              ErrorHandler::errorTypeToString(ErrorHandler::ErrorType::UNKNOWN_ERROR));
}

TEST_F(ErrorHandlerTest, ExecuteWithErrorHandling_SuccessfulFunction_ReturnsTrue) {
    bool function_called = false;
    
    bool result = ErrorHandler::executeWithErrorHandling(
        [&function_called]() {
            function_called = true;
        },
        "test context"
    );
    
    EXPECT_TRUE(result);
    EXPECT_TRUE(function_called);
}

TEST_F(ErrorHandlerTest, ExecuteWithErrorHandling_ThrowingFunction_HandlesException) {
    bool result = ErrorHandler::executeWithErrorHandling(
        []() {
            throw std::runtime_error("Test exception");
        },
        "test context"
    );
    
    EXPECT_FALSE(result);
    
    std::string output = cout_stream.str() + cerr_stream.str();
    EXPECT_TRUE(output.find("Exception in test context") != std::string::npos);
    EXPECT_TRUE(output.find("Test exception") != std::string::npos);
}

TEST_F(ErrorHandlerTest, ExecuteWithErrorHandling_WithRecovery_AttemptsRecovery) {
    bool result = ErrorHandler::executeWithErrorHandling(
        []() {
            throw std::runtime_error("Test exception");
        },
        "test context",
        createRecoveryCallback(true)
    );
    
    EXPECT_TRUE(result);
    EXPECT_TRUE(recovery_called);
}

TEST_F(ErrorHandlerTest, ExecuteWithErrorHandling_UnknownException_HandlesGracefully) {
    bool result = ErrorHandler::executeWithErrorHandling(
        []() {
            throw 42; // Non-standard exception
        },
        "test context"
    );
    
    EXPECT_FALSE(result);
    
    std::string output = cout_stream.str() + cerr_stream.str();
    EXPECT_TRUE(output.find("Unknown exception in test context") != std::string::npos);
}

TEST_F(ErrorHandlerTest, RecoveryCallback_ThrowsException_HandlesGracefully) {
    auto throwing_recovery = []() -> bool {
        throw std::runtime_error("Recovery failed");
    };
    
    bool result = ErrorHandler::handleError(
        ErrorHandler::ErrorType::CONNECTION_LOST,
        "Test error",
        throwing_recovery
    );
    
    EXPECT_FALSE(result);
    
    std::string output = cout_stream.str() + cerr_stream.str();
    EXPECT_TRUE(output.find("Exception during error recovery") != std::string::npos);
    EXPECT_TRUE(output.find("Recovery failed") != std::string::npos);
}

TEST_F(ErrorHandlerTest, RecoveryCallback_ThrowsUnknownException_HandlesGracefully) {
    auto throwing_recovery = []() -> bool {
        throw 42; // Non-standard exception
    };
    
    bool result = ErrorHandler::handleError(
        ErrorHandler::ErrorType::CONNECTION_LOST,
        "Test error",
        throwing_recovery
    );
    
    EXPECT_FALSE(result);
    
    std::string output = cout_stream.str() + cerr_stream.str();
    EXPECT_TRUE(output.find("Unknown exception during error recovery") != std::string::npos);
}