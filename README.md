# OPC UA to HTTP

## introduction

this project use open62541 to connect opc ua server.
and use crow to serve http.
and use nlohmann-json to parse json.

## usage

### starting the application

**Basic usage:**
```bash
opcua2http.exe
```

**Command-line options:**
```bash
opcua2http.exe [OPTIONS]

Options:
  -h, --help              Show help message and exit
  -v, --version           Show version information and exit
  -c, --config            Show configuration information and exit
  -d, --debug             Enable debug logging
  -q, --quiet             Suppress non-error output
  --log-level LEVEL       Set log level (trace, debug, info, warn, error, critical)
```

**Examples:**
```bash
# Start with default settings
opcua2http.exe

# Start with debug logging
opcua2http.exe --debug

# Start with trace logging
opcua2http.exe --log-level trace

# Start in quiet mode (only errors)
opcua2http.exe --quiet

# Show all configuration options
opcua2http.exe --config
```

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


## configuration

Use environment variables to configure the application. See `.env.example` for a complete configuration template.

### core OPC UA configuration

```bash
OPC_ENDPOINT=opc.tcp://127.0.0.1:4840      # OPC UA Server URL
OPC_SECURITY_MODE=1                        # 1:None, 2:Sign, 3:SignAndEncrypt
OPC_SECURITY_POLICY=None                   # Security policy
OPC_NAMESPACE=2                            # Default namespace for Node IDs
OPC_APPLICATION_URI=urn:CLIENT:NodeOPCUA-Client # Client application URI
```

### connection configuration

```bash
CONNECTION_RETRY_MAX=5                     # Max retries per connection attempt
CONNECTION_INITIAL_DELAY=1000              # Initial delay before first attempt (ms)
CONNECTION_MAX_RETRY=10                    # Global max reconnection attempts (-1 for infinite)
CONNECTION_MAX_DELAY=10000                 # Max delay between retries (ms)
CONNECTION_RETRY_DELAY=5000                # Base delay between retries (ms)
```

### web server configuration

```bash
SERVER_PORT=3000                           # Port the gateway will listen on
```

### security configuration

```bash
API_KEY=your_api_key_here                  # Secret key for X-API-Key authentication
AUTH_USERNAME=admin                        # User for Basic Authentication
AUTH_PASSWORD=your_secure_password         # Password for Basic Authentication
ALLOWED_ORIGINS=http://localhost:3000,https://example.com # Allowed CORS origins
```

### intelligent cache configuration

The system implements a three-tier cache strategy that balances response speed with data freshness:

#### cache states

1. **FRESH** (age < refresh threshold)
   - Returns cached data immediately
   - No server access required
   - Fastest response time

2. **STALE** (refresh threshold ≤ age < expire time)
   - Returns cached data immediately
   - Schedules background update asynchronously
   - Fast response + automatic refresh

3. **EXPIRED** (age ≥ expire time)
   - Synchronously reads from OPC UA server
   - Updates cache before responding
   - Ensures data freshness

#### cache timing parameters

```bash
# When cache data becomes "stale" and triggers background updates
# Default: 3 seconds
CACHE_REFRESH_THRESHOLD_SECONDS=3

# When cache data becomes "expired" and requires synchronous refresh
# Default: 10 seconds (must be > CACHE_REFRESH_THRESHOLD_SECONDS)
CACHE_EXPIRE_SECONDS=10

# How often to remove expired entries from cache
# Default: 60 seconds
CACHE_CLEANUP_INTERVAL_SECONDS=60
```

#### background update configuration

```bash
# Number of worker threads for background cache updates
# Default: 3, Range: 1-50
BACKGROUND_UPDATE_THREADS=3

# Maximum size of background update queue
# Default: 1000, Range: 1-100000
BACKGROUND_UPDATE_QUEUE_SIZE=1000

# Timeout for background update operations (milliseconds)
# Default: 5000 (5 seconds), Range: 1-300000
BACKGROUND_UPDATE_TIMEOUT_MS=5000
```

#### performance tuning

```bash
# Maximum number of cache entries
# Default: 10000, Range: 1-1000000
CACHE_MAX_ENTRIES=10000

# Maximum cache memory usage (MB)
# Default: 100, Range: 1-10240
CACHE_MAX_MEMORY_MB=100

# Maximum concurrent read operations
# Default: 10, Range: 1-1000
CACHE_CONCURRENT_READS=10
```

#### OPC UA optimization

```bash
# OPC UA read operation timeout (milliseconds)
# Default: 5000, Range: 1-300000
OPC_READ_TIMEOUT_MS=5000

# OPC UA batch read size
# Default: 50, Range: 1-1000
OPC_BATCH_SIZE=50

# OPC UA connection pool size
# Default: 5, Range: 1-100
OPC_CONNECTION_POOL_SIZE=5
```

### cache configuration examples

#### high-frequency data (fast-changing values)

For rapidly changing sensor readings or real-time measurements:

```bash
CACHE_REFRESH_THRESHOLD_SECONDS=1
CACHE_EXPIRE_SECONDS=3
CACHE_CLEANUP_INTERVAL_SECONDS=30
```

**Characteristics**: Very fresh data (max 1s old), frequent updates, higher server load

#### balanced configuration (default)

For general-purpose applications:

```bash
CACHE_REFRESH_THRESHOLD_SECONDS=3
CACHE_EXPIRE_SECONDS=10
CACHE_CLEANUP_INTERVAL_SECONDS=60
```

**Characteristics**: Good balance between freshness and performance

#### low-frequency data (slow-changing values)

For configuration values or status flags:

```bash
CACHE_REFRESH_THRESHOLD_SECONDS=10
CACHE_EXPIRE_SECONDS=30
CACHE_CLEANUP_INTERVAL_SECONDS=120
```

**Characteristics**: Longer cache validity, minimal updates, lower server load

#### high-performance (maximum speed)

For high-traffic applications prioritizing speed:

```bash
CACHE_REFRESH_THRESHOLD_SECONDS=5
CACHE_EXPIRE_SECONDS=60
CACHE_CLEANUP_INTERVAL_SECONDS=300
```

**Characteristics**: Most requests from cache, minimal synchronous reads, lowest server load

### monitoring cache performance

Check cache metrics via the `/health` endpoint:

```json
{
  "cache": {
    "hit_ratio": 0.95,
    "fresh_entries": 150,
    "stale_entries": 30,
    "expired_entries": 5,
    "efficiency_score": 0.92
  }
}
```

**Key metrics:**
- `hit_ratio`: Percentage of requests served from cache (higher is better)
- `fresh_entries`: Number of entries in FRESH state
- `stale_entries`: Number of entries in STALE state (being refreshed)
- `expired_entries`: Number of entries requiring synchronous refresh
- `efficiency_score`: Overall cache effectiveness (0.0 to 1.0)

### logging configuration

```bash
# Log level: TRACE, DEBUG, INFO, WARNING, ERROR, CRITICAL
# Default: INFO
LOG_LEVEL=INFO
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

## deployment

For production deployment, Docker setup, and detailed configuration:
- **[DEPLOYMENT.md](DEPLOYMENT.md)** - Complete deployment guide
- **[MAINTENANCE.md](MAINTENANCE.md)** - Maintenance and troubleshooting

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
