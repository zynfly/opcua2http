# Technology Stack

## Build System

- **CMake**: Primary build system (minimum version 4.0)
- **vcpkg**: Package manager for C++ dependencies
- **C++20**: Language standard for modern C++ features

## Core Dependencies

### OPC UA Communication
- **open62541**: Open-source OPC UA library for client functionality
  - Handles OPC UA protocol communication
  - Provides subscription and monitoring capabilities
  - Thread-safe operations with UA_THREADSAFE

### HTTP Server
- **Crow**: Lightweight C++ web framework
  - RESTful API implementation
  - Built-in threading and request handling
  - CORS and middleware support

### JSON Processing
- **nlohmann-json**: Modern C++ JSON library
  - Automatic serialization/deserialization
  - Type-safe JSON operations
  - Header-only library

## Common Build Commands

### Initial Setup
```bash
# Configure with vcpkg toolchain
cmake -B cmake-build-debug -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build the project
cmake --build cmake-build-debug
```

### Development Workflow
```bash
# Clean build
cmake --build cmake-build-debug --target clean

# Build with verbose output
cmake --build cmake-build-debug --verbose

# Run the executable
./cmake-build-debug/opcua2http.exe
```

## Configuration Management

All runtime configuration is handled through environment variables:

### Required Environment Variables
- `OPC_ENDPOINT`: OPC UA server URL
- `SERVER_PORT`: HTTP server port (default: 3000)

### Optional Security Variables
- `API_KEY`: API key for authentication
- `AUTH_USERNAME`/`AUTH_PASSWORD`: Basic auth credentials
- `ALLOWED_ORIGINS`: CORS allowed origins

### Connection Parameters
- `CONNECTION_RETRY_MAX`: Maximum retry attempts
- `CONNECTION_RETRY_DELAY`: Delay between retries (ms)
- `OPC_SECURITY_MODE`: Security mode (1=None, 2=Sign, 3=SignAndEncrypt)

## Architecture Patterns

- **Component-based design**: Separate managers for cache, subscriptions, and reconnection
- **Thread-safe operations**: Using std::shared_mutex for concurrent access
- **RAII**: Proper resource management for OPC UA client and HTTP server
- **Event-driven**: Callback-based OPC UA subscription handling