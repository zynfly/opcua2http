cmake_minimum_required(VERSION 3.20)
project(opcua2http)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(open62541 REQUIRED)
find_package(Crow REQUIRED)
find_package(nlohmann_json REQUIRED)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    main.cpp
    src/config/Configuration.cpp
    src/core/OPCUAHTTPBridge.cpp
)

# Create executable
add_executable(opcua2http ${SOURCES})

# Link libraries
target_link_libraries(opcua2http 
    PRIVATE 
    open62541::open62541
    Crow::Crow
    nlohmann_json::nlohmann_json
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
