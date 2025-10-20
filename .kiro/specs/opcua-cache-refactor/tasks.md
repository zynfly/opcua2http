# Implementation Plan

- [x] 1. Enhance Configuration System for Cache Settings






  - Add new cache timing configuration parameters (CACHE_REFRESH_THRESHOLD_SECONDS, CACHE_EXPIRE_SECONDS, CACHE_CLEANUP_INTERVAL_SECONDS)
  - Implement configuration validation for cache timing parameters
  - Add background update configuration (thread count, queue size, timeout)
  - Add performance tuning parameters (max entries, memory limits, concurrent reads)
  - _Requirements: 8.1, 8.2, 8.3, 8.4_

- [x] 2. Enhance CacheManager with Smart Cache Timing




  - [x] 2.1 Add cache age evaluation methods to CacheEntry


    - Implement isWithinRefreshThreshold(), isExpired(), and getAge() methods
    - Add creationTime field to track cache entry age
    - Modify CacheEntry structure to support new timing logic
    - _Requirements: 1.2, 1.3, 1.4, 2.1_

  - [x] 2.2 Implement CacheStatus enumeration and smart retrieval


    - Add FRESH, STALE, EXPIRED cache status evaluation
    - Implement getCachedValueWithStatus() method for intelligent cache decisions
    - Add batch cache operations with status evaluation (getCachedValuesWithStatus)
    - _Requirements: 1.2, 1.3, 1.4, 3.1_

  - [x] 2.3 Add configurable cache timing parameters


    - Replace hardcoded expiration with configurable refresh threshold and expire time
    - Implement setRefreshThreshold(), setExpireTime(), setCleanupInterval() methods
    - Update constructor to accept timing configuration
    - _Requirements: 8.1, 8.2, 8.3_



  - [x] 2.4 Optimize concurrency control mechanisms





    - Replace exclusive locks with shared_mutex for concurrent reads where possible
    - Use atomic operations for simple state changes and statistics
    - Implement lock-free statistics updates
    - Add batch update operations (updateCacheBatch)
    - _Requirements: 9.1, 9.2, 9.3, 9.4_

- [x] 3. Create ReadStrategy Component





  - [x] 3.1 Implement core ReadStrategy class


    - Create ReadStrategy interface with processNodeRequests() and processNodeRequest() methods
    - Implement cache status evaluation and decision logic for FRESH/STALE/EXPIRED states
    - Add batch processing optimization with BatchReadPlan structure
    - _Requirements: 1.2, 1.3, 1.4, 3.1, 3.2_

  - [x] 3.2 Implement concurrency control for duplicate requests


    - Add mutex-based protection against concurrent reads of same NodeId
    - Implement request deduplication using activeReads set and condition variables
    - Add acquireReadLock() and releaseReadLock() helper methods
    - _Requirements: 3.4, 7.1, 7.2_

  - [x] 3.3 Add batch optimization and planning


    - Implement createBatchPlan() to categorize nodes by cache status
    - Create executeBatchPlan() for optimized batch processing
    - Add intelligent grouping of nodes for OPC UA batch reads
    - _Requirements: 3.1, 3.2, 3.3_

- [x] 4. Create BackgroundUpdater Component





  - [x] 4.1 Implement background update queue and threading


    - Create BackgroundUpdater class with worker thread pool
    - Implement update queue with deduplication logic
    - Add scheduleUpdate() and scheduleBatchUpdate() methods
    - _Requirements: 1.3, 3.2_

  - [x] 4.2 Add background update configuration and control


    - Implement start(), stop(), and configuration methods
    - Add setMaxConcurrentUpdates(), setUpdateQueueSize(), setUpdateTimeout()
    - Create UpdateStats structure for monitoring background updates
    - _Requirements: 8.4_

  - [x] 4.3 Implement update statistics and monitoring


    - Add statistics tracking for background updates (success/failure rates)
    - Implement getStats() method for monitoring
    - Add logging for background update activities
    - _Requirements: 6.1, 6.2_

- [x] 5. Modify APIHandler to Use ReadStrategy





  - [x] 5.1 Replace SubscriptionManager with ReadStrategy


    - Remove SubscriptionManager dependency from APIHandler constructor
    - Add ReadStrategy dependency and update component initialization
    - Modify processNodeRequests() to use ReadStrategy instead of cache+subscription logic
    - _Requirements: 4.1, 4.2, 4.3_

  - [x] 5.2 Implement enhanced error handling for cache-based system


    - Add handleOPCConnectionError() method for connection failure scenarios
    - Implement cache fallback when OPC UA server is unavailable
    - Create buildCacheErrorResponse() for cache-specific error responses
    - _Requirements: 5.1, 5.2, 5.3, 5.4_

  - [x] 5.3 Maintain complete API compatibility


    - ✅ Ensure /iotgateway/read endpoint behavior remains identical
    - ✅ Updated JSON response format to use full field names (nodeId, success, quality, value, timestamp_iso)
    - ✅ Keep authentication, CORS, and error response formats unchanged
    - ✅ Updated integration tests to use new JSON format
    - ✅ Updated documentation to reflect correct API format
    - _Requirements: 4.1, 4.2, 4.3, 4.5, 4.6_

- [ ] 6. Enhance OPCUAClient for Cache-Based Operations
  - [x] 6.1 Implement batch reading capabilities
    - Add readNodesBatch() method for efficient multi-node reads
    - Optimize OPC UA client for batch operations instead of individual reads
    - Implement connection pooling for concurrent batch operations
    - _Requirements: 3.2, 3.3_

  - [x] 6.2 Add enhanced connection state management
    - Implement detailed ConnectionState enum (CONNECTED, DISCONNECTED, CONNECTING, ERROR)
    - Add getConnectionState() and getLastError() methods for cache fallback decisions
    - Implement connection timeout and retry configuration
    - _Requirements: 5.1, 5.4_

  - [x] 6.3 Remove subscription-related functionality
    - Remove all subscription management code from OPCUAClient
    - Simplify client to focus only on read operations
    - Clean up unused subscription callbacks and state management
    - _Requirements: Design Architecture Changes_

- [x] 7. Update OPCUAHTTPBridge Main Application
  - [x] 7.1 Remove SubscriptionManager and ReconnectionManager
    - Remove SubscriptionManager from bridge initialization
    - Simplify ReconnectionManager or remove if no longer needed
    - Update component dependency chain in initializeComponents()
    - _Requirements: Design Architecture Changes_

  - [x] 7.2 Add ReadStrategy and BackgroundUpdater initialization
    - Add ReadStrategy initialization in initializeComponents()
    - Initialize BackgroundUpdater and start background threads
    - Update component cleanup in destructor and stop() method
    - _Requirements: Design Architecture Changes_

  - [x] 7.3 Update configuration loading and validation
    - Load new cache configuration parameters from environment
    - Add validation for cache timing and performance parameters
    - Update getStatus() method to include cache statistics
    - _Requirements: 8.1, 8.2, 8.3, 8.4_

- [x] 8. Implement Cache Statistics and Monitoring
  - [x] 8.1 Create enhanced CacheStatistics structure
    - Add performance metrics (cache hits, misses, stale refreshes, expired reads)
    - Implement timing metrics (response times for different cache states)
    - Add cache health metrics (fresh/stale/expired entry counts)
    - _Requirements: 6.1, 6.2, 6.3_

  - [x] 8.2 Add CacheMetrics collection system
    - Implement real-time metrics recording methods
    - Add recordCacheHit(), recordCacheMiss(), recordStaleRefresh(), recordExpiredRead()
    - Create getMetricsJSON() for API status endpoints
    - _Requirements: 6.1, 6.2, 6.3, 6.4_

  - [x] 8.3 Enhance health check endpoints
    - Update /health and /status endpoints with cache metrics
    - Add cache efficiency and health indicators
    - Include background update statistics in status responses
    - _Requirements: 6.3_

- [x] 9. Implement Error Handling and Fallback Mechanisms
  - [x] 9.1 Create CacheErrorHandler component
    - Implement ErrorAction enum (RETURN_CACHED, RETURN_ERROR, RETRY_CONNECTION)
    - Add determineAction() method for error scenario decision making
    - Create handleConnectionError() for OPC UA connection failures
    - _Requirements: 5.1, 5.2, 5.3, 5.4_

  - [x] 9.2 Implement cache fallback strategies
    - Return cached data when OPC UA server is unavailable (if cache exists)
    - Add cache age indicators in error responses
    - Implement graceful degradation for partial OPC UA failures
    - _Requirements: 5.1, 5.2, 5.3_

  - [x] 9.3 Add comprehensive error logging and monitoring
    - Log cache fallback scenarios and connection errors
    - Add error rate monitoring and alerting thresholds
    - Implement error recovery statistics tracking
    - _Requirements: 5.4, 6.4_

- [x] 10. Performance Optimization and Memory Management
  - [x] 10.1 Implement cache memory management
    - Add CacheMemoryManager with configurable memory limits
    - Implement LRU eviction when memory pressure occurs
    - Add memory usage monitoring and reporting
    - _Requirements: 9.3, Design Performance Considerations_

  - [x] 10.2 Optimize batch processing performance
    - Implement intelligent batching based on cache states
    - Add connection pooling for concurrent OPC UA operations
    - Optimize lock granularity for high-concurrency scenarios
    - _Requirements: 3.1, 3.2, 3.3, 7.3, 7.4, 9.1, 9.2_

  - [x] 10.3 Add performance monitoring and tuning
    - Implement performance benchmarking for cache operations
    - Add concurrency metrics (wait times, lock contention)
    - Create performance tuning recommendations based on metrics
    - _Requirements: 6.1, 6.2, 7.4_

- [x] 11. Fix API Cache Strategy Implementation
  - [x] 11.1 Implement correct cache miss handling
    - When cache has no data (CACHE_MISS), directly read from OPC UA server
    - Update cache with fresh data and return response immediately
    - Add proper error handling for OPC UA read failures
    - _Requirements: 1.4, 2.1_

  - [x] 11.2 Implement stale cache handling with background update
    - When cache data exists but exceeds refresh threshold (STALE, 3-10 seconds)
    - Return cached data immediately to client for fast response
    - Schedule background update task to refresh cache asynchronously
    - Ensure background update doesn't block API response
    - _Requirements: 1.3, 3.2_

  - [x] 11.3 Implement expired cache handling
    - When cache data exceeds expiration time (EXPIRED, > 10 seconds)
    - Treat as cache miss and synchronously read from OPC UA server
    - Update cache with fresh data before returning response
    - Remove or update expired entries during the process
    - _Requirements: 1.4, 2.2, 2.3_

  - [x] 11.4 Update ReadStrategy to follow correct flow
    - Modify processNodeRequest() to implement the three-path strategy
    - Ensure proper cache status evaluation (FRESH/STALE/EXPIRED/MISS)
    - Coordinate between synchronous reads and background updates
    - Add logging to track which path each request takes
    - _Requirements: 1.2, 1.3, 1.4, 3.1_

- [x] 12. Core Testing and Validation
  - [x] 12.1 Essential unit tests
    - Test CacheManager timing logic (fresh/stale/expired states)
    - Test ReadStrategy batch processing and concurrency control
    - Test error handling and cache fallback scenarios
    - _Requirements: 1.2, 1.3, 1.4, 3.1, 3.4, 5.1, 5.2_

  - [x] 12.2 Integration testing
    - Test end-to-end cache flow with real OPC UA server
    - Verify API compatibility (/iotgateway/read endpoint)
    - Test concurrent requests and batch operations
    - Test OPC UA disconnection and recovery
    - _Requirements: 4.1, 4.2, 7.1, 7.2_

  - [x] 12.3 Basic performance validation
    - Verify cache hit response times are acceptable
    - Test system stability under normal load
    - Validate memory usage is reasonable
    - _Requirements: 6.1, 6.2, 7.4_

- [ ] 13. Update README.md Documentation
  - Update README.md with comprehensive documentation including:
    - Cache-based architecture overview and timing behavior (3s refresh, 10s expiration)
    - API endpoint documentation with cache behavior details
    - Configuration parameters reference (all CACHE_* environment variables)
    - Monitoring and cache statistics interpretation
    - Performance tuning guidelines and best practices
    - Error handling and fallback behavior
    - Deployment guide and example configurations
    - Troubleshooting guide for common issues
  - _Requirements: 4.1, 5.1, 5.2, 6.1, 6.2, 6.3, 8.1, 8.2, 8.3, 8.4_
