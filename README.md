# OPC UA to HTTP

## introduction

this project use open62541 to connect opc ua server.
and use crow to serve http.
and use nlohmann-json to parse json.

## usage

### API endpoint

Read OPC UA values:

```
GET /iotgateway/read?ids=<node-id1>,<node-id2>,...
```

**Query Parameters:**
- `ids` (required): One or more OPC UA Node IDs, comma-separated
  - Examples: `ns=2;s=MyVariable`, `ns=3;i=1001`, `ns=2;s=Temperature,ns=2;s=Pressure`

### response format

**Successful Response (200 OK):**
```json
{
  "readResults": [
    {
      "id": "ns=2;s=MyVariable",
      "s": true,
      "r": "Good",
      "v": "123.45",
      "t": 1678886400000
    }
  ]
}
```

**Response Fields:**
- `id`: Original Node ID from request
- `s`: Success status (true/false)
- `r`: Status description ("Good", "BadNodeIdUnknown", etc.)
- `v`: Read value (as string)
- `t`: Timestamp in milliseconds (OPC UA source timestamp)

**Error Response (400 Bad Request):**
```json
{
  "error": "Parameter 'ids' is required"
}
```

### usage examples

**Single node:**
```bash
curl "http://localhost:3000/iotgateway/read?ids=ns=2;s=Temperature"
```

**Multiple nodes:**
```bash
curl "http://localhost:3000/iotgateway/read?ids=ns=2;s=Temperature,ns=2;s=Pressure,ns=2;i=1001"
```

**With API key authentication:**
```bash
curl -H "X-API-Key: your_api_key_here" \
     "http://localhost:3000/iotgateway/read?ids=ns=2;s=Temperature"
```

**With basic authentication:**
```bash
curl -u admin:password \
     "http://localhost:3000/iotgateway/read?ids=ns=2;s=Temperature"
```

### health check

Check service status:
```bash
curl "http://localhost:3000/health"
```

Response:
```json
{
  "service": "opcua-http-bridge",
  "status": "running",
  "uptime_seconds": 3600,
  "opc_connected": true,
  "cached_items": 25,
  "active_subscriptions": 25
}
```


## config

use environment variables to config.

```
# === Core OPC UA Configuration ===
OPC_ENDPOINT=opc.tcp://127.0.0.1:4840      # OPC UA Server URL
OPC_SECURITY_MODE=1                        # 1:None, 2:Sign, 3:SignAndEncrypt
OPC_SECURITY_POLICY=None                   # None, Basic128Rsa15, Basic256, Basic256Sha256, Aes128_Sha256_RsaOaep, Aes256_Sha256_RsaPss
OPC_NAMESPACE=2                            # Default namespace for Node IDs (if not specified)
OPC_APPLICATION_URI=urn:CLIENT:NodeOPCUA-Client # Client application URI

# === OPC UA Connection Configuration ===
CONNECTION_RETRY_MAX=5                     # Max retries per connection attempt
CONNECTION_INITIAL_DELAY=1000              # Initial delay before first attempt (ms)
CONNECTION_MAX_RETRY=10                    # Global max reconnection attempts (-1 for infinite)
CONNECTION_MAX_DELAY=10000                 # Max delay between retries (ms)
CONNECTION_RETRY_DELAY=5000                # Base delay between retries (ms)

# === Web Server Configuration ===
SERVER_PORT=3000                           # Port the gateway will listen on

# === API Security Configuration ===
API_KEY=your_api_key_here                  # Secret key for X-API-Key authentication
AUTH_USERNAME=admin                        # User for Basic Authentication
AUTH_PASSWORD=your_secure_password         # Password for Basic Authentication
ALLOWED_ORIGINS=http://localhost:3000,[https://your-frontend-domain.com](https://your-frontend-domain.com) # Allowed CORS origins (comma-separated)
```

## testing

### running tests

This project includes comprehensive unit and integration tests to verify system functionality.

#### build and run all tests

```bash
# Build tests
cmake --build cmake-build-debug --target opcua2http_tests

# Run all tests
cmake-build-debug/Debug/opcua2http_tests.exe
```

#### run specific test categories

```bash
# Run unit tests only
cmake-build-debug/Debug/opcua2http_tests.exe --gtest_filter="*Test.*" --gtest_filter="-*IntegrationTest*" --gtest_filter="-*PerformanceTest*"

# Run integration tests only
cmake-build-debug/Debug/opcua2http_tests.exe --gtest_filter="*IntegrationTest*"

# Run performance tests only
cmake-build-debug/Debug/opcua2http_tests.exe --gtest_filter="*PerformanceTest*"

# Run specific test
cmake-build-debug/Debug/opcua2http_tests.exe --gtest_filter="EndToEndIntegrationTest.BasicDataFlow"
```

### test types

#### unit tests
- **Cache Manager**: Thread-safe caching operations and expiration
- **OPC UA Client**: Connection, data reading, and error handling
- **Subscription Manager**: Subscription lifecycle and data notifications
- **API Handler**: HTTP request processing and JSON responses
- **Reconnection Manager**: Automatic reconnection and recovery

#### integration tests
- **End-to-End Data Flow**: Complete HTTP to OPC UA communication
- **Subscription Mechanism**: Automatic subscription creation and cache updates
- **Cache Behavior**: Performance and data consistency verification
- **Error Handling**: Invalid request handling and system stability
- **Mixed Requests**: Batch processing with valid and invalid nodes

#### performance tests
- **Concurrent Requests**: Multiple simultaneous request handling (20+ requests)
- **Cache Performance**: Response time improvement with caching
- **Long-term Stability**: Extended operation testing (30+ seconds)
- **Memory Usage**: Resource management under load (100+ cached items)

### test configuration

Tests use a mock OPC UA server with predefined variables:
- **Node 1001**: Int32 test variable
- **Node 1002**: String test variable  
- **Node 1003**: Boolean test variable
- **Nodes 2000-2099**: Performance testing variables

### performance benchmarks

Expected performance on typical hardware:
- Concurrent requests: 20 requests complete within 5 seconds
- Cache improvement: Cached requests significantly faster than initial reads
- Stability: >95% success rate during extended testing
- Memory: Handle 100+ cached items without issues

## build

### prerequisites

this project use cmake to build and vcpkg to manage dependencies.

#### required dependencies
- **open62541**: OPC UA client library
- **Crow**: HTTP web framework
- **nlohmann-json**: JSON processing
- **spdlog**: Logging framework
- **GTest**: Testing framework (for tests)

### build commands

```bash
# Configure with vcpkg toolchain
cmake -B cmake-build-debug -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build main application
cmake --build cmake-build-debug --target opcua2http

# Build with tests
cmake --build cmake-build-debug --target opcua2http_tests
```

### build options

- `BUILD_TESTS=ON/OFF`: Enable/disable test compilation (default: ON)
- `CMAKE_BUILD_TYPE=Debug/Release`: Build configuration
