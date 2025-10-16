# Project Structure

## Current Layout

```
opcua2http/
├── .idea/                    # IDE configuration files
├── .kiro/                    # Kiro AI assistant configuration
│   ├── steering/            # AI guidance documents
│   └── specs/               # Feature specifications
├── cmake-build-debug/       # CMake build output directory
├── CMakeLists.txt          # Build configuration
├── main.cpp                # Application entry point
├── README.md               # Project documentation
└── vcpkg.json             # Package dependencies
```

## Planned Structure (Implementation)

```
opcua2http/
├── include/                 # Header files
│   ├── cache/
│   │   └── CacheManager.h
│   ├── subscription/
│   │   └── SubscriptionManager.h
│   ├── opcua/
│   │   └── OPCUAClient.h
│   ├── http/
│   │   └── APIHandler.h
│   ├── reconnection/
│   │   └── ReconnectionManager.h
│   ├── config/
│   │   └── Configuration.h
│   └── core/
│       ├── ReadResult.h
│       └── OPCUAHTTPBridge.h
├── src/                     # Source files
│   ├── cache/
│   ├── subscription/
│   ├── opcua/
│   ├── http/
│   ├── reconnection/
│   ├── config/
│   └── core/
├── tests/                   # Unit and integration tests
│   ├── unit/
│   └── integration/
└── docs/                    # Additional documentation
```

## File Organization Principles

### Header Files (`include/`)
- **Modular design**: Each component has its own subdirectory
- **Clear interfaces**: Public APIs in header files only
- **Forward declarations**: Minimize include dependencies
- **Thread safety**: Document thread-safety requirements in headers

### Source Files (`src/`)
- **Implementation separation**: Mirror the include directory structure
- **Single responsibility**: Each source file handles one component
- **Error handling**: Consistent error handling patterns across all modules

### Key Components

#### Core Application (`core/`)
- `OPCUAHTTPBridge.h/cpp`: Main application class
- `ReadResult.h`: Data structures for OPC UA results
- `Configuration.h/cpp`: Environment variable configuration

#### OPC UA Layer (`opcua/`)
- `OPCUAClient.h/cpp`: open62541 client wrapper
- Handles connection management and data reading
- Provides callback interfaces for subscriptions

#### Caching System (`cache/`)
- `CacheManager.h/cpp`: Thread-safe map-based caching
- Manages data lifecycle and cleanup
- Provides concurrent access patterns

#### Subscription Management (`subscription/`)
- `SubscriptionManager.h/cpp`: OPC UA subscription lifecycle
- On-demand subscription creation
- Automatic cleanup of unused subscriptions

#### HTTP API (`http/`)
- `APIHandler.h/cpp`: Crow-based REST API implementation
- Authentication and CORS handling
- JSON request/response processing

#### Reconnection Logic (`reconnection/`)
- `ReconnectionManager.h/cpp`: Connection monitoring and recovery
- Exponential backoff retry logic
- Subscription restoration after reconnection

## Naming Conventions

### Files and Classes
- **PascalCase** for class names: `CacheManager`, `OPCUAClient`
- **camelCase** for method names: `getCachedValue()`, `addMonitoredItem()`
- **snake_case** for member variables: `cache_mutex_`, `connection_retry_max_`

### Constants and Enums
- **UPPER_SNAKE_CASE** for constants: `DEFAULT_SERVER_PORT`
- **PascalCase** for enum classes: `ErrorType::CONNECTION_LOST`

### Namespaces
- Use `opcua2http` namespace for all project code
- Nested namespaces for major components: `opcua2http::cache`, `opcua2http::http`

## Build Integration

### CMake Structure
- Main `CMakeLists.txt` in root directory
- Separate target for main executable
- Link all required vcpkg dependencies
- Enable C++20 features and appropriate compiler flags

### Testing Integration
- Separate test executable target
- Mock OPC UA server for integration tests
- Unit test framework integration (future enhancement)