#pragma once

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include "OPCUATestBase.h"

namespace opcua2http {
namespace test {

/**
 * @brief Global test environment for managing shared resources
 * 
 * This environment is set up once before all tests and torn down
 * after all tests complete. It manages the shared mock OPC UA server
 * and ensures global state (like spdlog) is properly initialized.
 */
class GlobalTestEnvironment : public ::testing::Environment {
public:
    /**
     * @brief Set up the global test environment
     * 
     * Sets up a stable spdlog logger. The shared mock server is initialized
     * lazily when first needed by a test.
     */
    void SetUp() override {
        // Initialize spdlog with a stable default logger
        // This prevents issues when tests modify the logger
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("test_default", console_sink);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        
        // Don't initialize shared mock server here - let it be lazy-initialized
        // This prevents conflicts with tests that use independent mock servers
    }
    
    /**
     * @brief Tear down the global test environment
     * 
     * Shuts down the shared mock OPC UA server.
     */
    void TearDown() override {
        OPCUATestBase::shutdownSharedMockServer();
    }
};

} // namespace test
} // namespace opcua2http
