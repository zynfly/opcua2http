# OPC UA to HTTP Gateway

A high-performance OPC UA to HTTP gateway with intelligent caching that bridges industrial OPC UA servers with modern web applications. The system implements a smart three-tier cache strategy (FRESH/STALE/EXPIRED) to balance response speed with data freshness.

## Key Features

- **Intelligent Cache Strategy**: 3-second refresh threshold, 10-second expiration
- **High Performance**: Sub-millisecond response times for cached data
- **Background Updates**: Asynchronous cache refresh for stale data
- **Graceful Degradation**: Returns cached data when OPC UA server is unavailable
- **Production Ready**: Comprehensive monitoring, error handling, and performance tuning

## Quick Start

```bash
# Start with default settings
opcua2http.exe

# Start with debug logging
opcua2http.exe --debug

# Show configuration
opcua2http.exe --config
```

## Table of Contents

- [Architecture](#architecture)
- [API Reference](#api-reference)
- [Configuration](#configuration)
- [Performance Tuning](#performance-tuning)
- [Error Handling](#error-handling)
- [Troubleshooting](#troubleshooting)
- [Deployment](#deployment)
- [Testing](#testing)
- [Building](#building)

## Architecture

### Cache-Based Design

The gateway implements a three-tier intelligent caching system:

```
┌─────────────────────────────────────────────────────────────┐
│                    Client Request                            │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
         ┌────────────────────────┐
         │   Check Cache Status   │
         └────────────┬───────────┘
                      │
        ┌─────────────┼─────────────┐
        │             │             │
        ▼             ▼             ▼
   ┌────────┐   ┌─────────┐   ┌──────────┐
   │ FRESH  │   │  STALE  │   │ EXPIRED  │
   │ <3s    │   │ 3-10s   │   │  >10s    │
   └───┬────┘   └────┬────┘   └────┬─────┘
       │             │              │
       ▼             ▼              ▼
  Return      Return Cache    Read from
  Cache       + Background    OPC Server
  Immediately    Update       Synchronously
```

### Cache States

| State | Age | Behavior | Response Time | Use Case |
|-------|-----|----------|---------------|----------|
| **FRESH** | < 3s | Return cached data immediately | < 1ms | High-frequency requests |
| **STALE** | 3-10s | Return cache + schedule background update | < 1ms | Balance speed & freshness |
| **EXPIRED** | > 10s | Read from OPC UA server synchronously | 50-200ms | Ensure data currency |

### Technology Stack

- **open62541**: OPC UA client library
- **Crow**: HTTP web framework
- **nlohmann-json**: JSON processing
- **spdlog**: Logging framework

## API Reference

### Read OPC UA Values

```
GET /iotgateway/read?ids=<node-id1>,<node-id2>,...
```

**Query Parameters:**
- `ids` (required): Comma-separated OPC UA Node IDs
  - Format: `ns=X;s=Name` or `ns=X;i=Number`
  - Example: `ns=2;s=Temperature,ns=2;s=Pressure`

**Cache Behavior:**
- Each node evaluated independently for cache status
- Batch requests optimize OPC UA server access
- Concurrent requests for same node are deduplicated

**Success Response (200 OK):**
```json
{
  "readResults": [
    {
      "nodeId": "ns=2;s=Temperature",
      "success": true,
      "quality": "Good",
      "value": "23.5",
      "timestamp_iso": "2024-03-15T10:30:00.000Z"
    }
  ]
}
```

**Response Fields:**
- `nodeId`: Original Node ID from request
- `success`: Boolean success status
- `quality`: OPC UA quality status ("Good", "BadNodeIdUnknown", etc.)
- `value`: Read value as string
- `timestamp_iso`: ISO 8601 timestamp from OPC UA server

**Error Response (400 Bad Request):**
```json
{
  "error": {
    "code": 400,
    "message": "Bad Request",
    "details": "Missing 'ids' parameter",
    "type": "bad_request",
    "help": "Check request parameters and format"
  }
}
```

**Cache Fallback Response (503 Service Unavailable):**

When OPC UA server is unavailable but cached data exists:

```json
{
  "error": {
    "code": 503,
    "message": "OPC UA Service Temporarily Unavailable",
    "node_id": "ns=2;s=Temperature",
    "cache_info": {
      "has_cached_data": true,
      "cache_age_seconds": 15,
      "fallback_used": true
    },
    "help": "Cached data returned due to OPC UA server unavailability",
    "retry_after": 30
  }
}
```

### Health Check

```
GET /health
```

**Response:**
```json
{
  "status": "ok",
  "timestamp": 1710500400000,
  "uptime_seconds": 3600,
  "opc_connected": true,
  "opc_endpoint": "opc.tcp://127.0.0.1:4840",
  "cached_items": 185,
  "version": "1.0.0",
  "cache": {
    "hit_ratio": 0.95,
    "fresh_entries": 150,
    "stale_entries": 30,
    "expired_entries": 5,
    "efficiency_score": 0.92,
    "is_healthy": true
  }
}
```

**Health Status Values:**
- `ok`: System operating normally
- `degraded`: System functional but with warnings (low cache hit ratio, etc.)

**Cache Health Indicators:**
- `hit_ratio`: Percentage of requests served from cache (target: > 0.85)
- `efficiency_score`: Overall cache effectiveness (target: > 0.80)
- `is_healthy`: Boolean indicating if cache metrics are within healthy ranges

### Status Endpoint

```
GET /status
```

Returns detailed system statistics including OPC UA connection, cache metrics, HTTP API statistics, and error handling information.

### Usage Examples

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

## Configuration

Configure the application using environment variables. See `.env.example` for a complete template.

### Core OPC UA Settings

```bash
OPC_ENDPOINT=opc.tcp://127.0.0.1:4840      # OPC UA Server URL
OPC_SECURITY_MODE=1                        # 1:None, 2:Sign, 3:SignAndEncrypt
OPC_SECURITY_POLICY=None                   # Security policy
OPC_NAMESPACE=2                            # Default namespace for Node IDs
OPC_APPLICATION_URI=urn:CLIENT:NodeOPCUA-Client # Client application URI
```

### Connection Settings

```bash
CONNECTION_RETRY_MAX=5                     # Max retries per connection attempt
CONNECTION_INITIAL_DELAY=1000              # Initial delay before first attempt (ms)
CONNECTION_MAX_RETRY=5                     # Max reconnection attempts per cycle
CONNECTION_MAX_DELAY=10000                 # Max delay between retries (ms)
CONNECTION_RETRY_DELAY=500                 # Base delay between retries (ms)
```

#### Connection Retry Behavior

The system uses a two-phase retry strategy when the OPC UA server connection is lost:

**Phase 1: Exponential Backoff (Attempts 1 to CONNECTION_MAX_RETRY)**
- Delay increases exponentially with each attempt
- Formula: `CONNECTION_RETRY_DELAY * 2^(attempt-1)`
- Capped at `CONNECTION_MAX_DELAY` (default: 10 seconds)
- Example with defaults: 500ms → 1s → 2s → 4s → 8s

**Phase 2: Reset and Retry**
- After reaching `CONNECTION_MAX_RETRY` attempts, system waits `2 * CONNECTION_MAX_DELAY`
- Retry counter is reset to 0
- Phase 1 begins again with exponential backoff

**Example Timeline** (with default settings):
```
0.0s:  Attempt 1 (delay: 500ms)
0.5s:  Attempt 2 (delay: 1s)
1.5s:  Attempt 3 (delay: 2s)
3.5s:  Attempt 4 (delay: 4s)
7.5s:  Attempt 5 (delay: 8s) - Max retries reached
27.5s: Wait period ends, counter reset
28.0s: Attempt 1 begins again (delay: 500ms)
...cycle repeats...
```

**Important Notes**:
- There is a 20-second gap (2 * 10s) between retry cycles by default
- The system will continue retrying indefinitely until connection is restored
- If your OPC UA server typically has longer downtimes, consider:
  - Increasing `CONNECTION_MAX_RETRY` to reduce gap frequency
  - Adjusting `CONNECTION_MAX_DELAY` to control the maximum wait time
- For most scenarios, the default settings provide good balance between responsiveness and server load

### Web Server Settings

```bash
SERVER_PORT=3000                           # HTTP server port
```

### Security Settings

```bash
API_KEY=your_api_key_here                  # Secret key for X-API-Key authentication
AUTH_USERNAME=admin                        # User for Basic Authentication
AUTH_PASSWORD=your_secure_password         # Password for Basic Authentication
ALLOWED_ORIGINS=http://localhost:3000,https://example.com # Allowed CORS origins
```

### Cache Configuration

#### Cache Timing Parameters

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

#### Background Update Configuration

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

#### Performance Tuning

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

#### OPC UA Optimization

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

### Logging Configuration

```bash
# Log level: TRACE, DEBUG, INFO, WARNING, ERROR, CRITICAL
# Default: INFO
LOG_LEVEL=INFO
```

### Configuration Examples

#### High-Frequency Data (fast-changing values)

For rapidly changing sensor readings:

```bash
CACHE_REFRESH_THRESHOLD_SECONDS=1
CACHE_EXPIRE_SECONDS=3
CACHE_CLEANUP_INTERVAL_SECONDS=30
```

**Characteristics**: Very fresh data (max 1s old), frequent updates, higher server load

#### Balanced Configuration (default)

For general-purpose applications:

```bash
CACHE_REFRESH_THRESHOLD_SECONDS=3
CACHE_EXPIRE_SECONDS=10
CACHE_CLEANUP_INTERVAL_SECONDS=60
```

**Characteristics**: Good balance between freshness and performance

#### Low-Frequency Data (slow-changing values)

For configuration values or status flags:

```bash
CACHE_REFRESH_THRESHOLD_SECONDS=10
CACHE_EXPIRE_SECONDS=30
CACHE_CLEANUP_INTERVAL_SECONDS=120
```

**Characteristics**: Longer cache validity, minimal updates, lower server load

#### High-Performance (maximum speed)

For high-traffic applications prioritizing speed:

```bash
CACHE_REFRESH_THRESHOLD_SECONDS=5
CACHE_EXPIRE_SECONDS=60
CACHE_CLEANUP_INTERVAL_SECONDS=300
```

**Characteristics**: Most requests from cache, minimal synchronous reads, lowest server load

## Performance Tuning

### Interpreting Cache Metrics

**Healthy System Indicators:**
- Hit ratio > 0.85 (85% of requests from cache)
- Efficiency score > 0.80
- Fresh entries > 70% of total entries
- Average response time < 5ms
- Failed background updates < 1%

**Warning Signs:**
- Hit ratio < 0.70 (too many cache misses)
- Expired entries > 20% of total (cache timing too aggressive)
- Failed updates > 5% (OPC UA server issues)
- Memory usage approaching limit

### Tuning for Different Scenarios

#### High-Frequency Polling (many clients, same data)

```bash
CACHE_REFRESH_THRESHOLD_SECONDS=5
CACHE_EXPIRE_SECONDS=60
CACHE_CONCURRENT_READS=20
BACKGROUND_UPDATE_THREADS=5
```

- Longer cache validity reduces server load
- More concurrent reads handle traffic spikes
- More background threads keep cache fresh

#### Real-Time Data (fast-changing values)

```bash
CACHE_REFRESH_THRESHOLD_SECONDS=1
CACHE_EXPIRE_SECONDS=3
BACKGROUND_UPDATE_THREADS=10
OPC_BATCH_SIZE=100
```

- Short cache times ensure freshness
- Many background threads for rapid updates
- Large batch size for efficiency

#### Low-Latency Priority (minimize response time)

```bash
CACHE_REFRESH_THRESHOLD_SECONDS=10
CACHE_EXPIRE_SECONDS=120
CACHE_MAX_ENTRIES=50000
CACHE_MAX_MEMORY_MB=500
```

- Long cache validity maximizes cache hits
- Large cache capacity reduces evictions
- Most requests served from cache

#### Resource-Constrained Environment

```bash
CACHE_MAX_ENTRIES=1000
CACHE_MAX_MEMORY_MB=20
BACKGROUND_UPDATE_THREADS=1
CACHE_CONCURRENT_READS=5
```

- Limited cache size reduces memory usage
- Fewer threads reduce CPU usage
- Lower concurrency reduces overhead

### Performance Best Practices

1. **Monitor Hit Ratio**: Aim for > 85% cache hit ratio
   - If too low: Increase `CACHE_EXPIRE_SECONDS`
   - If too high but data stale: Decrease `CACHE_REFRESH_THRESHOLD_SECONDS`

2. **Optimize Batch Size**: Match your typical request patterns
   - Small requests (1-10 nodes): `OPC_BATCH_SIZE=20`
   - Large requests (50+ nodes): `OPC_BATCH_SIZE=100`

3. **Balance Background Updates**: Avoid overwhelming OPC UA server
   - Start with 3 threads, increase if queue builds up
   - Monitor `queued_updates` in health endpoint

4. **Memory Management**: Prevent cache evictions
   - Set `CACHE_MAX_ENTRIES` to 2x your typical node count
   - Monitor `memory_usage_mb` and adjust `CACHE_MAX_MEMORY_MB`

5. **Connection Pooling**: Match concurrent load
   - `OPC_CONNECTION_POOL_SIZE` = `BACKGROUND_UPDATE_THREADS` + 2

## Error Handling

### Graceful Degradation

The system implements intelligent error handling to maintain service availability even when the OPC UA server is unavailable.

### Error Scenarios

**1. OPC UA Server Disconnected (with cached data)**

Returns cached data with error indicator (HTTP 503):
- Client receives last known good value
- `cache_info` indicates fallback was used
- `cache_age_seconds` shows data age

**2. OPC UA Server Disconnected (no cached data)**

Returns error without data (HTTP 503):
- No cached data available
- Client should retry later
- `retry_after` header indicates wait time

**3. Invalid Node ID**

Returns error for specific node (HTTP 200 with error in result):
- `success`: false
- `quality`: "BadNodeIdUnknown"
- Other nodes in batch may succeed

**4. Batch Request with Mixed Results**

Each node processed independently (HTTP 200):
- Check individual `success` flags
- Some nodes may succeed while others fail
- Partial success is normal

### Automatic Recovery

**Connection Recovery:**
- Automatic reconnection with exponential backoff
- Configurable retry limits and delays
- Background updates resume after reconnection

**Cache Fallback Strategy:**
1. Try to read from OPC UA server
2. If connection fails and cache exists → return cached data
3. If connection fails and no cache → return error
4. Background updates continue attempting refresh

### Error Logging

All errors logged with appropriate severity:

```
[ERROR] OPC UA connection failed: Connection timeout
[WARN] Returning cached data (age: 15s) due to connection error
[INFO] OPC UA connection restored, resuming normal operations
[DEBUG] Background update failed for node ns=2;s=Temperature, will retry
```

## Troubleshooting

### Common Issues

#### Issue: "Connection Error - No Cached Data Available"

**Symptoms:**
- API returns 503 errors
- OPC UA server unreachable
- No cached data available

**Diagnosis:**
```bash
curl "http://localhost:3000/health"
# Look for: "opc_connected": false
```

**Solutions:**
1. Verify OPC UA server is running and accessible
2. Check `OPC_ENDPOINT` configuration
3. Verify network connectivity and firewall rules
4. Check OPC UA server security settings
5. Review `CONNECTION_RETRY_MAX` and `CONNECTION_MAX_DELAY`

#### Issue: High Response Times

**Symptoms:**
- API responses > 100ms
- Low cache hit ratio (< 70%)
- Many expired cache entries

**Diagnosis:**
```bash
curl "http://localhost:3000/health" | grep -A 10 "cache"
# Look for: low hit_ratio, high expired_entries
```

**Solutions:**
1. Increase `CACHE_EXPIRE_SECONDS` to keep data cached longer
2. Increase `CACHE_MAX_ENTRIES` if cache is full
3. Optimize `OPC_BATCH_SIZE` for your request patterns
4. Add more `BACKGROUND_UPDATE_THREADS` if queue building up
5. Check OPC UA server performance

#### Issue: Stale Data Being Returned

**Symptoms:**
- Data not updating frequently enough
- High number of stale entries
- Background update queue building up

**Solutions:**
1. Increase `BACKGROUND_UPDATE_THREADS` (default: 3)
2. Increase `BACKGROUND_UPDATE_QUEUE_SIZE` if queue is full
3. Decrease `CACHE_REFRESH_THRESHOLD_SECONDS` for more aggressive updates
4. Increase `BACKGROUND_UPDATE_TIMEOUT_MS` if updates timing out
5. Check OPC UA server load and response times

#### Issue: Memory Usage Growing

**Symptoms:**
- Memory usage approaching `CACHE_MAX_MEMORY_MB`
- System performance degrading
- Cache evictions occurring frequently

**Solutions:**
1. Reduce `CACHE_EXPIRE_SECONDS` for faster cleanup
2. Reduce `CACHE_CLEANUP_INTERVAL_SECONDS` for more frequent cleanup
3. Increase `CACHE_MAX_MEMORY_MB` if system has available RAM
4. Reduce `CACHE_MAX_ENTRIES` to limit cache size
5. Review which nodes are being cached

#### Issue: Background Updates Failing

**Symptoms:**
- High `failed_updates` count
- Stale data persisting
- Errors in logs about background updates

**Solutions:**
1. Verify OPC UA server is stable and responsive
2. Increase `BACKGROUND_UPDATE_TIMEOUT_MS` (default: 5000ms)
3. Reduce `BACKGROUND_UPDATE_THREADS` to lower server load
4. Check for invalid node IDs in cache
5. Review OPC UA server logs for errors

### Debug Mode

Enable detailed logging:

```bash
# Start with debug logging
opcua2http.exe --debug

# Or with trace logging (very verbose)
opcua2http.exe --log-level trace

# Set via environment variable
LOG_LEVEL=DEBUG opcua2http.exe
```

**Debug log includes:**
- Cache hit/miss decisions
- Background update scheduling
- OPC UA read operations
- Connection state changes
- Performance metrics

### Getting Help

If issues persist:

1. **Collect diagnostic information:**
   ```bash
   # Get current configuration
   opcua2http.exe --config > config.txt

   # Get health status
   curl "http://localhost:3000/health" > health.json
   ```

2. **Check common configuration mistakes:**
   - `CACHE_REFRESH_THRESHOLD_SECONDS` must be < `CACHE_EXPIRE_SECONDS`
   - `OPC_ENDPOINT` must be a valid OPC UA URL
   - Port `SERVER_PORT` must not be in use

## Deployment

### Production Deployment Checklist

Before deploying to production:

- [ ] Configure appropriate cache timing for your use case
- [ ] Set secure `API_KEY`, `AUTH_USERNAME`, and `AUTH_PASSWORD`
- [ ] Configure `ALLOWED_ORIGINS` for CORS
- [ ] Set `LOG_LEVEL=INFO` or `WARN` (not DEBUG/TRACE)
- [ ] Configure connection retry parameters
- [ ] Set appropriate `CACHE_MAX_ENTRIES` and `CACHE_MAX_MEMORY_MB`
- [ ] Test OPC UA server connectivity
- [ ] Verify all required node IDs are accessible
- [ ] Set up monitoring for `/health` endpoint
- [ ] Configure log rotation and retention
- [ ] Test failover behavior (OPC UA server down)
- [ ] Load test with expected traffic patterns
- [ ] Document your specific configuration

### Example Production Configuration

```bash
# OPC UA Connection
OPC_ENDPOINT=opc.tcp://production-server:4840
OPC_SECURITY_MODE=3
OPC_SECURITY_POLICY=Basic256Sha256

# Cache Configuration (balanced)
CACHE_REFRESH_THRESHOLD_SECONDS=3
CACHE_EXPIRE_SECONDS=10
CACHE_CLEANUP_INTERVAL_SECONDS=60

# Performance Tuning
CACHE_MAX_ENTRIES=50000
CACHE_MAX_MEMORY_MB=500
CACHE_CONCURRENT_READS=20
BACKGROUND_UPDATE_THREADS=5
BACKGROUND_UPDATE_QUEUE_SIZE=5000

# OPC UA Optimization
OPC_READ_TIMEOUT_MS=10000
OPC_BATCH_SIZE=100
OPC_CONNECTION_POOL_SIZE=7

# Security
API_KEY=<generate-secure-random-key>
AUTH_USERNAME=admin
AUTH_PASSWORD=<generate-secure-password>
ALLOWED_ORIGINS=https://your-app.com,https://dashboard.your-app.com

# Logging
LOG_LEVEL=INFO

# Server
SERVER_PORT=3000
```

### Additional Resources

- **[DEPLOYMENT.md](DEPLOYMENT.md)** - Complete deployment guide
- **[MAINTENANCE.md](MAINTENANCE.md)** - Maintenance procedures
- **[QUICKSTART.md](QUICKSTART.md)** - Quick start guide

## Testing

### Running Tests

This project includes comprehensive unit and integration tests.

#### Build and Run All Tests

```bash
# Build tests
cmake --build cmake-build-debug --target opcua2http_tests

# Run all tests
cmake-build-debug/Debug/opcua2http_tests.exe
```

#### Run Specific Test Categories

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

### Test Types

#### Unit Tests
- **Cache Manager**: Thread-safe caching operations and expiration
- **OPC UA Client**: Connection, data reading, and error handling
- **API Handler**: HTTP request processing and JSON responses
- **Read Strategy**: Cache-based read logic and background updates

#### Integration Tests
- **End-to-End Data Flow**: Complete HTTP to OPC UA communication
- **Cache Behavior**: Performance and data consistency verification
- **Error Handling**: Invalid request handling and system stability
- **Mixed Requests**: Batch processing with valid and invalid nodes

#### Performance Tests
- **Concurrent Requests**: Multiple simultaneous request handling (20+ requests)
- **Cache Performance**: Response time improvement with caching
- **Long-term Stability**: Extended operation testing (30+ seconds)
- **Memory Usage**: Resource management under load (100+ cached items)

### Test Configuration

Tests use a mock OPC UA server with predefined variables:
- **Node 1001**: Int32 test variable
- **Node 1002**: String test variable
- **Node 1003**: Boolean test variable
- **Nodes 2000-2099**: Performance testing variables

### Performance Benchmarks

Expected performance on typical hardware:
- Concurrent requests: 20 requests complete within 5 seconds
- Cache improvement: Cached requests significantly faster than initial reads
- Stability: >95% success rate during extended testing
- Memory: Handle 100+ cached items without issues

## Building

### Prerequisites

This project uses CMake to build and vcpkg to manage dependencies.

#### Required Dependencies
- **open62541**: OPC UA client library
- **Crow**: HTTP web framework
- **nlohmann-json**: JSON processing
- **spdlog**: Logging framework
- **GTest**: Testing framework (for tests)

### Build Commands

```bash
# Configure with vcpkg toolchain
cmake -B cmake-build-debug -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build main application
cmake --build cmake-build-debug --target opcua2http

# Build with tests
cmake --build cmake-build-debug --target opcua2http_tests
```

### Build Options

- `BUILD_TESTS=ON/OFF`: Enable/disable test compilation (default: ON)
- `CMAKE_BUILD_TYPE=Debug/Release`: Build configuration

## Command-Line Options

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

## License

[Your License Here]

## Contributing

[Your Contributing Guidelines Here]
