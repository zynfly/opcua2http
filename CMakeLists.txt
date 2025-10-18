cmake_minimum_required(VERSION 3.20)
project(opcua2http)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(open62541 REQUIRED)
find_package(Crow REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    main.cpp
    src/config/Configuration.cpp
    src/core/OPCUAHTTPBridge.cpp
    src/core/ErrorHandler.cpp
    src/core/OPCUALogBridge.cpp
    src/opcua/OPCUAClient.cpp
    src/cache/CacheManager.cpp
    src/subscription/SubscriptionManager.cpp
    src/reconnection/ReconnectionManager.cpp
    src/http/APIHandler.cpp
)

# Create executable
add_executable(opcua2http ${SOURCES})

# Link libraries
target_link_libraries(opcua2http 
    PRIVATE 
    open62541::open62541
    Crow::Crow
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

# Compiler-specific options
if(MSVC)
    target_compile_options(opcua2http PRIVATE /W4)
    target_compile_definitions(opcua2http PRIVATE _WIN32_WINNT=0x0601)
else()
    target_compile_options(opcua2http PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Enable threading support
find_package(Threads REQUIRED)
target_link_libraries(opcua2http PRIVATE Threads::Threads)

# Testing configuration
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    # Find Google Test
    find_package(GTest REQUIRED)
    
    # Enable testing
    enable_testing()
    
    # Common test infrastructure
    set(TEST_COMMON_SOURCES
        tests/common/MockOPCUAServer.cpp
        tests/common/OPCUATestBase.cpp
    )
    
    # Test source files
    set(TEST_SOURCES
        tests/test_main.cpp
        tests/unit/test_cache_manager.cpp
        tests/unit/test_opcua_client.cpp
        tests/unit/test_opcua_log_bridge.cpp
        tests/unit/test_subscription_manager.cpp
        tests/unit/test_api_handler.cpp
        tests/unit/test_error_handler.cpp
        src/config/Configuration.cpp
        src/core/ErrorHandler.cpp
        src/core/OPCUALogBridge.cpp
        src/opcua/OPCUAClient.cpp
        src/cache/CacheManager.cpp
        src/subscription/SubscriptionManager.cpp
        src/reconnection/ReconnectionManager.cpp
        src/http/APIHandler.cpp
        ${TEST_COMMON_SOURCES}
    )
    
    # Create test executable
    add_executable(opcua2http_tests ${TEST_SOURCES})
    
    # Link test libraries
    target_link_libraries(opcua2http_tests 
        PRIVATE 
        open62541::open62541
        Crow::Crow
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        GTest::gtest
        GTest::gmock
        Threads::Threads
    )
    
    # Include directories for tests
    target_include_directories(opcua2http_tests PRIVATE include tests)
    
    # Compiler-specific options for tests
    if(MSVC)
        target_compile_options(opcua2http_tests PRIVATE /W4)
        target_compile_definitions(opcua2http_tests PRIVATE _WIN32_WINNT=0x0601)
    else()
        target_compile_options(opcua2http_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    
    # Register tests with CTest
    include(GoogleTest)
    gtest_discover_tests(opcua2http_tests)
endif()
