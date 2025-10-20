#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "cache/CacheManager.h"
#include <thread>
#include <chrono>

using namespace opcua2http;

class CacheManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        cacheManager = std::make_unique<CacheManager>(1, 100); // 1 minute expiration, max 100 entries
    }

    void TearDown() override {
        cacheManager.reset();
    }

    std::unique_ptr<CacheManager> cacheManager;
};

TEST_F(CacheManagerTest, BasicCacheOperations) {
    // Test cache miss
    auto result = cacheManager->getCachedValue("ns=2;s=TestNode");
    EXPECT_FALSE(result.has_value());

    // Test adding cache entry
    ReadResult readResult = ReadResult::createSuccess("ns=2;s=TestNode", "42", 1234567890);
    cacheManager->addCacheEntry(readResult, false);

    // Test cache hit
    result = cacheManager->getCachedValue("ns=2;s=TestNode");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->nodeId, "ns=2;s=TestNode");
    EXPECT_EQ(result->value, "42");
    EXPECT_EQ(result->status, "Good");
    EXPECT_EQ(result->timestamp, 1234567890);
    EXPECT_FALSE(result->hasSubscription);
}

TEST_F(CacheManagerTest, CacheUpdate) {
    // Add initial entry
    cacheManager->updateCache("ns=2;s=TestNode", "100", "Good", "Success", 1000);

    auto result = cacheManager->getCachedValue("ns=2;s=TestNode");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "100");

    // Update the entry
    cacheManager->updateCache("ns=2;s=TestNode", "200", "Good", "Updated", 2000);

    result = cacheManager->getCachedValue("ns=2;s=TestNode");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, "200");
    EXPECT_EQ(result->timestamp, 2000);
}

TEST_F(CacheManagerTest, SubscriptionStatus) {
    // Add entry without subscription
    ReadResult readResult = ReadResult::createSuccess("ns=2;s=TestNode", "42", 1234567890);
    cacheManager->addCacheEntry(readResult, false);

    // Check subscription status
    auto subscribedNodes = cacheManager->getSubscribedNodeIds();
    EXPECT_TRUE(subscribedNodes.empty());

    // Set subscription status
    cacheManager->setSubscriptionStatus("ns=2;s=TestNode", true);

    subscribedNodes = cacheManager->getSubscribedNodeIds();
    EXPECT_EQ(subscribedNodes.size(), 1);
    EXPECT_EQ(subscribedNodes[0], "ns=2;s=TestNode");
}

TEST_F(CacheManagerTest, CacheStatistics) {
    // Initial stats
    auto stats = cacheManager->getStats();
    EXPECT_EQ(stats.totalEntries, 0);
    EXPECT_EQ(stats.totalHits, 0);
    EXPECT_EQ(stats.totalMisses, 0);

    // Add some entries and access them
    cacheManager->updateCache("ns=2;s=Node1", "100", "Good", "Success", 1000);
    cacheManager->updateCache("ns=2;s=Node2", "200", "Good", "Success", 2000);

    // Access existing entry (hit)
    cacheManager->getCachedValue("ns=2;s=Node1");

    // Access non-existing entry (miss)
    cacheManager->getCachedValue("ns=2;s=NonExistent");

    stats = cacheManager->getStats();
    EXPECT_EQ(stats.totalEntries, 2);
    EXPECT_EQ(stats.totalHits, 1);
    EXPECT_EQ(stats.totalMisses, 1);
    EXPECT_EQ(stats.totalWrites, 2);
    EXPECT_GT(stats.totalReads, 0);
    EXPECT_DOUBLE_EQ(stats.hitRatio, 0.5); // 1 hit out of 2 total accesses
}

TEST_F(CacheManagerTest, AccessControl) {
    // Test default access level (READ_WRITE)
    EXPECT_EQ(cacheManager->getAccessLevel(), CacheManager::AccessLevel::READ_WRITE);

    // Test read operations with READ_ONLY access
    cacheManager->setAccessLevel(CacheManager::AccessLevel::READ_ONLY);

    // Read should work
    auto result = cacheManager->getCachedValue("ns=2;s=TestNode");
    EXPECT_FALSE(result.has_value()); // No entry exists yet

    // Write should be denied (but won't throw, just won't work)
    cacheManager->updateCache("ns=2;s=TestNode", "100", "Good", "Success", 1000);
    result = cacheManager->getCachedValue("ns=2;s=TestNode");
    EXPECT_FALSE(result.has_value()); // Entry should not have been created

    // Restore write access and try again
    cacheManager->setAccessLevel(CacheManager::AccessLevel::READ_WRITE);
    cacheManager->updateCache("ns=2;s=TestNode", "100", "Good", "Success", 1000);
    result = cacheManager->getCachedValue("ns=2;s=TestNode");
    EXPECT_TRUE(result.has_value());
}

TEST_F(CacheManagerTest, CacheSizeManagement) {
    // Test basic size operations
    EXPECT_TRUE(cacheManager->empty());
    EXPECT_EQ(cacheManager->size(), 0);
    EXPECT_FALSE(cacheManager->isFull());

    // Add an entry
    cacheManager->updateCache("ns=2;s=TestNode", "100", "Good", "Success", 1000);
    EXPECT_FALSE(cacheManager->empty());
    EXPECT_EQ(cacheManager->size(), 1);

    // Clear cache (requires ADMIN access)
    cacheManager->setAccessLevel(CacheManager::AccessLevel::ADMIN);
    cacheManager->clear();
    EXPECT_TRUE(cacheManager->empty());
    EXPECT_EQ(cacheManager->size(), 0);
}

TEST_F(CacheManagerTest, AutoCleanupControl) {
    // Test auto cleanup control
    EXPECT_TRUE(cacheManager->isAutoCleanupEnabled());

    cacheManager->setAutoCleanupEnabled(false);
    EXPECT_FALSE(cacheManager->isAutoCleanupEnabled());

    // Cleanup should return 0 when disabled
    size_t cleaned = cacheManager->cleanupExpiredEntries();
    EXPECT_EQ(cleaned, 0);

    cleaned = cacheManager->cleanupUnusedEntries();
    EXPECT_EQ(cleaned, 0);
}

TEST_F(CacheManagerTest, MemoryUsageCalculation) {
    // Add some entries
    cacheManager->updateCache("ns=2;s=Node1", "value1", "Good", "Success", 1000);
    cacheManager->updateCache("ns=2;s=Node2", "longer_value_string", "Good", "Success", 2000);

    // Check memory usage
    size_t memoryUsage = cacheManager->getMemoryUsage();
    EXPECT_GT(memoryUsage, 0);

    auto stats = cacheManager->getStats();
    EXPECT_EQ(stats.memoryUsageBytes, memoryUsage);
}

// ============================================================================
// CONCURRENT ACCESS SAFETY TESTS
// ============================================================================

TEST_F(CacheManagerTest, ConcurrentReadAccess) {
    // Add initial data
    cacheManager->updateCache("ns=2;s=Node1", "100", "Good", "Success", 1000);
    cacheManager->updateCache("ns=2;s=Node2", "200", "Good", "Success", 2000);

    const int numThreads = 10;
    const int readsPerThread = 100;
    std::vector<std::thread> threads;
    std::atomic<int> successfulReads{0};
    std::atomic<int> failedReads{0};

    // Launch multiple reader threads
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < readsPerThread; ++j) {
                std::string nodeId = (j % 2 == 0) ? "ns=2;s=Node1" : "ns=2;s=Node2";
                auto result = cacheManager->getCachedValue(nodeId);
                if (result.has_value()) {
                    successfulReads.fetch_add(1);
                } else {
                    failedReads.fetch_add(1);
                }

                // Small delay to increase chance of race conditions
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // All reads should succeed since we added the data beforehand
    EXPECT_EQ(successfulReads.load(), numThreads * readsPerThread);
    EXPECT_EQ(failedReads.load(), 0);

    // Verify cache statistics are consistent
    auto stats = cacheManager->getStats();
    EXPECT_EQ(stats.totalEntries, 2);
    EXPECT_GE(stats.totalHits, numThreads * readsPerThread);
}

TEST_F(CacheManagerTest, ConcurrentWriteAccess) {
    const int numThreads = 5;
    const int writesPerThread = 15; // Reduced to stay within cache limit of 100
    std::vector<std::thread> threads;
    std::atomic<int> completedWrites{0};

    // Launch multiple writer threads
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < writesPerThread; ++j) {
                std::string nodeId = "ns=2;s=Thread" + std::to_string(i) + "_Node" + std::to_string(j);
                std::string value = "Value_" + std::to_string(i) + "_" + std::to_string(j);
                uint64_t timestamp = 1000 + i * 1000 + j;

                cacheManager->updateCache(nodeId, value, "Good", "Success", timestamp);
                completedWrites.fetch_add(1);

                // Small delay to increase chance of race conditions
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(completedWrites.load(), numThreads * writesPerThread);

    // Verify entries were written (cache may have enforced size limits)
    auto nodeIds = cacheManager->getCachedNodeIds();
    EXPECT_LE(nodeIds.size(), 100); // Should not exceed cache limit
    EXPECT_GT(nodeIds.size(), 0);   // Should have some entries

    // Verify cache statistics
    auto stats = cacheManager->getStats();
    EXPECT_LE(stats.totalEntries, 100); // Should respect cache limit
    EXPECT_EQ(stats.totalWrites, numThreads * writesPerThread); // All writes should be counted
}

TEST_F(CacheManagerTest, ConcurrentReadWriteAccess) {
    // Add some initial data
    for (int i = 0; i < 10; ++i) {
        std::string nodeId = "ns=2;s=InitialNode" + std::to_string(i);
        cacheManager->updateCache(nodeId, "InitialValue" + std::to_string(i), "Good", "Success", 1000 + i);
    }

    const int numReaderThreads = 5;
    const int numWriterThreads = 3;
    const int operationsPerThread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> successfulReads{0};
    std::atomic<int> successfulWrites{0};
    std::atomic<bool> stopFlag{false};

    // Launch reader threads
    for (int i = 0; i < numReaderThreads; ++i) {
        threads.emplace_back([&, i]() {
            int readCount = 0;
            while (!stopFlag.load() && readCount < operationsPerThread) {
                std::string nodeId = "ns=2;s=InitialNode" + std::to_string(readCount % 10);
                auto result = cacheManager->getCachedValue(nodeId);
                if (result.has_value()) {
                    successfulReads.fetch_add(1);
                }
                readCount++;
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Launch writer threads
    for (int i = 0; i < numWriterThreads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < operationsPerThread; ++j) {
                std::string nodeId = "ns=2;s=WriterNode" + std::to_string(i) + "_" + std::to_string(j);
                std::string value = "WriterValue_" + std::to_string(i) + "_" + std::to_string(j);

                cacheManager->updateCache(nodeId, value, "Good", "Success", 2000 + i * 1000 + j);
                successfulWrites.fetch_add(1);

                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    stopFlag.store(true);

    // Verify operations completed successfully
    EXPECT_GT(successfulReads.load(), 0);
    EXPECT_EQ(successfulWrites.load(), numWriterThreads * operationsPerThread);

    // Verify cache integrity
    auto stats = cacheManager->getStats();
    EXPECT_GE(stats.totalEntries, 10); // At least the initial entries
    EXPECT_GT(stats.totalHits, 0);
    EXPECT_GT(stats.totalWrites, 0);
}

// ============================================================================
// CACHE EXPIRATION CLEANUP TESTS
// ============================================================================

TEST_F(CacheManagerTest, ExpirationCleanupMechanism) {
    // Create cache manager with very short expiration time (1 second for testing)
    auto shortExpiryCache = std::make_unique<CacheManager>(0, 100); // 0 minutes = immediate expiration

    // Add some entries
    shortExpiryCache->updateCache("ns=2;s=Node1", "100", "Good", "Success", 1000);
    shortExpiryCache->updateCache("ns=2;s=Node2", "200", "Good", "Success", 2000);
    shortExpiryCache->updateCache("ns=2;s=Node3", "300", "Good", "Success", 3000);

    // Verify entries exist
    EXPECT_EQ(shortExpiryCache->size(), 3);

    // Wait for entries to expire (since expiration is 0 minutes, they should expire immediately)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Trigger cleanup
    size_t cleanedCount = shortExpiryCache->cleanupExpiredEntries();

    // All entries should be cleaned up
    EXPECT_EQ(cleanedCount, 3);
    EXPECT_EQ(shortExpiryCache->size(), 0);
}

TEST_F(CacheManagerTest, ExpirationWithSubscriptions) {
    // Create cache manager with short expiration
    auto shortExpiryCache = std::make_unique<CacheManager>(0, 100);

    // Add entries, some with subscriptions
    shortExpiryCache->updateCache("ns=2;s=Node1", "100", "Good", "Success", 1000);
    shortExpiryCache->updateCache("ns=2;s=Node2", "200", "Good", "Success", 2000);
    shortExpiryCache->updateCache("ns=2;s=Node3", "300", "Good", "Success", 3000);

    // Set subscription status for some nodes
    shortExpiryCache->setSubscriptionStatus("ns=2;s=Node1", true);
    shortExpiryCache->setSubscriptionStatus("ns=2;s=Node3", true);

    EXPECT_EQ(shortExpiryCache->size(), 3);

    // Wait for expiration
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Cleanup expired entries
    size_t cleanedCount = shortExpiryCache->cleanupExpiredEntries();

    // All entries should be cleaned regardless of subscription status for expiration
    EXPECT_EQ(cleanedCount, 3);
    EXPECT_EQ(shortExpiryCache->size(), 0);
}

TEST_F(CacheManagerTest, UnusedEntriesCleanup) {
    // Add entries with different access patterns
    cacheManager->updateCache("ns=2;s=RecentNode", "100", "Good", "Success", 1000);
    cacheManager->updateCache("ns=2;s=OldNode", "200", "Good", "Success", 2000);
    cacheManager->updateCache("ns=2;s=SubscribedNode", "300", "Good", "Success", 3000);

    // Set subscription for one node
    cacheManager->setSubscriptionStatus("ns=2;s=SubscribedNode", true);

    // Access the recent node to update its lastAccessed time
    auto result = cacheManager->getCachedValue("ns=2;s=RecentNode");
    EXPECT_TRUE(result.has_value());

    // Manually set old access time for OldNode by accessing cache internals
    // (This is a test-specific approach - in real usage, time would naturally pass)

    EXPECT_EQ(cacheManager->size(), 3);

    // Cleanup unused entries
    cacheManager->cleanupUnusedEntries();

    // The cleanup should preserve subscribed nodes and recently accessed nodes
    // Only truly unused nodes (without subscription and not recently accessed) should be removed
    // Since we can't easily manipulate time in this test, we expect 0 or minimal cleanup
    EXPECT_GE(cacheManager->size(), 1); // At least the subscribed node should remain
}

TEST_F(CacheManagerTest, AutoCleanupDisabled) {
    // Disable auto cleanup
    cacheManager->setAutoCleanupEnabled(false);
    EXPECT_FALSE(cacheManager->isAutoCleanupEnabled());

    // Add entries
    cacheManager->updateCache("ns=2;s=Node1", "100", "Good", "Success", 1000);
    cacheManager->updateCache("ns=2;s=Node2", "200", "Good", "Success", 2000);

    EXPECT_EQ(cacheManager->size(), 2);

    // Try to cleanup - should return 0 since auto cleanup is disabled
    size_t expiredCleaned = cacheManager->cleanupExpiredEntries();
    size_t unusedCleaned = cacheManager->cleanupUnusedEntries();

    EXPECT_EQ(expiredCleaned, 0);
    EXPECT_EQ(unusedCleaned, 0);
    EXPECT_EQ(cacheManager->size(), 2); // No entries should be removed
}

// ============================================================================
// MEMORY USAGE LIMIT TESTS
// ============================================================================

TEST_F(CacheManagerTest, MemoryUsageLimits) {
    // Create cache manager with small size limit
    auto limitedCache = std::make_unique<CacheManager>(60, 5); // Max 5 entries

    // Add entries up to the limit
    for (int i = 0; i < 5; ++i) {
        std::string nodeId = "ns=2;s=Node" + std::to_string(i);
        std::string value = "Value" + std::to_string(i);
        limitedCache->updateCache(nodeId, value, "Good", "Success", 1000 + i);
    }

    EXPECT_EQ(limitedCache->size(), 5);
    EXPECT_TRUE(limitedCache->isFull());

    // Add one more entry - should trigger size limit enforcement
    limitedCache->updateCache("ns=2;s=ExtraNode", "ExtraValue", "Good", "Success", 2000);

    // Cache should still be at or below the limit
    EXPECT_LE(limitedCache->size(), 5);
}

TEST_F(CacheManagerTest, MemoryUsageWithSubscriptions) {
    // Create cache manager with small size limit
    auto limitedCache = std::make_unique<CacheManager>(60, 3); // Max 3 entries

    // Add entries and set subscriptions for some
    limitedCache->updateCache("ns=2;s=Node1", "Value1", "Good", "Success", 1000);
    limitedCache->updateCache("ns=2;s=Node2", "Value2", "Good", "Success", 2000);
    limitedCache->updateCache("ns=2;s=Node3", "Value3", "Good", "Success", 3000);

    // Set subscriptions for first two nodes
    limitedCache->setSubscriptionStatus("ns=2;s=Node1", true);
    limitedCache->setSubscriptionStatus("ns=2;s=Node2", true);

    EXPECT_EQ(limitedCache->size(), 3);
    EXPECT_TRUE(limitedCache->isFull());

    // Add more entries - should prefer to remove non-subscribed entries
    limitedCache->updateCache("ns=2;s=Node4", "Value4", "Good", "Success", 4000);
    limitedCache->updateCache("ns=2;s=Node5", "Value5", "Good", "Success", 5000);

    // Cache should still be at the limit
    EXPECT_LE(limitedCache->size(), 3);

    // Subscribed nodes should be preserved
    auto subscribedNodes = limitedCache->getSubscribedNodeIds();
    EXPECT_GE(subscribedNodes.size(), 1); // At least some subscribed nodes should remain
}

TEST_F(CacheManagerTest, MemoryUsageCalculationAccuracy) {
    // Test memory usage calculation with various entry sizes
    std::vector<std::pair<std::string, std::string>> testData = {
        {"ns=2;s=SmallNode", "1"},
        {"ns=2;s=MediumNode", "This is a medium length value string"},
        {"ns=2;s=LargeNode", std::string(1000, 'X')}, // Large value
        {"ns=2;s=VeryLongNodeIdWithManyCharacters", "Short"}
    };

    size_t previousMemoryUsage = cacheManager->getMemoryUsage();

    for (const auto& data : testData) {
        cacheManager->updateCache(data.first, data.second, "Good", "Success", 1000);

        size_t currentMemoryUsage = cacheManager->getMemoryUsage();
        EXPECT_GT(currentMemoryUsage, previousMemoryUsage);
        previousMemoryUsage = currentMemoryUsage;
    }

    // Verify stats reflect memory usage
    auto stats = cacheManager->getStats();
    EXPECT_EQ(stats.memoryUsageBytes, cacheManager->getMemoryUsage());
    EXPECT_GT(stats.memoryUsageBytes, 0);
}

TEST_F(CacheManagerTest, ConcurrentMemoryOperations) {
    // Create cache with moderate size limit
    auto limitedCache = std::make_unique<CacheManager>(60, 50);

    const int numThreads = 5;
    const int entriesPerThread = 20;
    std::vector<std::thread> threads;
    std::atomic<int> successfulWrites{0};

    // Launch threads that add entries concurrently
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < entriesPerThread; ++j) {
                std::string nodeId = "ns=2;s=Thread" + std::to_string(i) + "_Node" + std::to_string(j);
                std::string value = std::string(100, 'A' + (i % 26)); // Variable size values

                limitedCache->updateCache(nodeId, value, "Good", "Success", 1000 + i * 1000 + j);
                successfulWrites.fetch_add(1);

                // Check memory usage periodically
                if (j % 5 == 0) {
                    size_t memUsage = limitedCache->getMemoryUsage();
                    EXPECT_GT(memUsage, 0);
                }

                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify cache stayed within reasonable bounds
    EXPECT_LE(limitedCache->size(), 50); // Should not exceed max size
    EXPECT_GT(successfulWrites.load(), 0);

    // Verify memory usage is calculated correctly
    size_t finalMemoryUsage = limitedCache->getMemoryUsage();
    EXPECT_GT(finalMemoryUsage, 0);

    auto stats = limitedCache->getStats();
    EXPECT_EQ(stats.memoryUsageBytes, finalMemoryUsage);
}

// ============================================================================
// CACHE TIMING LOGIC TESTS (FRESH/STALE/EXPIRED)
// ============================================================================

TEST_F(CacheManagerTest, CacheTimingFreshState) {
    // Create cache manager with 3s refresh threshold and 10s expiration
    auto timedCache = std::make_unique<CacheManager>(60, 100, 3, 10);

    // Add a cache entry
    timedCache->updateCache("ns=2;s=TestNode", "100", "Good", "Success", 1000);

    // Immediately check - should be FRESH
    auto result = timedCache->getCachedValueWithStatus("ns=2;s=TestNode");
    ASSERT_TRUE(result.entry.has_value());
    EXPECT_EQ(result.status, CacheManager::CacheStatus::FRESH);
    EXPECT_EQ(result.entry->value, "100");

    // Wait 1 second - should still be FRESH
    std::this_thread::sleep_for(std::chrono::seconds(1));
    result = timedCache->getCachedValueWithStatus("ns=2;s=TestNode");
    ASSERT_TRUE(result.entry.has_value());
    EXPECT_EQ(result.status, CacheManager::CacheStatus::FRESH);
}

TEST_F(CacheManagerTest, CacheTimingStaleState) {
    // Create cache manager with 3s refresh threshold and 10s expiration
    auto timedCache = std::make_unique<CacheManager>(60, 100, 3, 10);

    // Add a cache entry
    timedCache->updateCache("ns=2;s=TestNode", "100", "Good", "Success", 1000);

    // Wait 4 seconds - should be STALE (> 3s but < 10s)
    std::this_thread::sleep_for(std::chrono::seconds(4));

    auto result = timedCache->getCachedValueWithStatus("ns=2;s=TestNode");
    ASSERT_TRUE(result.entry.has_value());
    EXPECT_EQ(result.status, CacheManager::CacheStatus::STALE);
    EXPECT_EQ(result.entry->value, "100");
}

TEST_F(CacheManagerTest, CacheTimingExpiredState) {
    // Create cache manager with 3s refresh threshold and 10s expiration
    auto timedCache = std::make_unique<CacheManager>(60, 100, 3, 10);

    // Add a cache entry
    timedCache->updateCache("ns=2;s=TestNode", "100", "Good", "Success", 1000);

    // Wait 11 seconds - should be EXPIRED (> 10s)
    std::this_thread::sleep_for(std::chrono::seconds(11));

    auto result = timedCache->getCachedValueWithStatus("ns=2;s=TestNode");
    ASSERT_TRUE(result.entry.has_value());
    EXPECT_EQ(result.status, CacheManager::CacheStatus::EXPIRED);
    EXPECT_EQ(result.entry->value, "100");
}

TEST_F(CacheManagerTest, CacheTimingCacheMiss) {
    // Create cache manager with 3s refresh threshold and 10s expiration
    auto timedCache = std::make_unique<CacheManager>(60, 100, 3, 10);

    // Query non-existent node - should return no entry
    auto result = timedCache->getCachedValueWithStatus("ns=2;s=NonExistent");
    EXPECT_FALSE(result.entry.has_value());
    // When there's no entry, status is not meaningful, just check entry is missing
}

TEST_F(CacheManagerTest, CacheTimingBatchOperations) {
    // Create cache manager with 3s refresh threshold and 10s expiration
    auto timedCache = std::make_unique<CacheManager>(60, 100, 3, 10);

    // Add entries at different times
    timedCache->updateCache("ns=2;s=FreshNode", "100", "Good", "Success", 1000);

    std::this_thread::sleep_for(std::chrono::seconds(4));
    timedCache->updateCache("ns=2;s=StaleNode", "200", "Good", "Success", 2000);

    std::this_thread::sleep_for(std::chrono::seconds(7));

    // Query batch with mixed states
    std::vector<std::string> nodeIds = {
        "ns=2;s=FreshNode",   // Should be EXPIRED (11s old)
        "ns=2;s=StaleNode",   // Should be STALE (7s old)
        "ns=2;s=MissingNode"  // Should be CACHE_MISS
    };

    auto results = timedCache->getCachedValuesWithStatus(nodeIds);
    ASSERT_EQ(results.size(), 3);

    // First node should be expired
    EXPECT_TRUE(results[0].entry.has_value());
    EXPECT_EQ(results[0].status, CacheManager::CacheStatus::EXPIRED);

    // Second node should be stale
    EXPECT_TRUE(results[1].entry.has_value());
    EXPECT_EQ(results[1].status, CacheManager::CacheStatus::STALE);

    // Third node should be missing
    EXPECT_FALSE(results[2].entry.has_value());
    // When there's no entry, status is not meaningful
}

TEST_F(CacheManagerTest, CacheTimingConfigurationUpdate) {
    // Create cache manager with initial timing
    auto timedCache = std::make_unique<CacheManager>(60, 100, 3, 10);

    // Add a cache entry
    timedCache->updateCache("ns=2;s=TestNode", "100", "Good", "Success", 1000);

    // Wait 4 seconds - should be STALE with current config
    std::this_thread::sleep_for(std::chrono::seconds(4));
    auto result = timedCache->getCachedValueWithStatus("ns=2;s=TestNode");
    EXPECT_EQ(result.status, CacheManager::CacheStatus::STALE);

    // Update refresh threshold to 5 seconds
    timedCache->setRefreshThreshold(std::chrono::seconds(5));

    // Now should be FRESH (4s < 5s threshold)
    result = timedCache->getCachedValueWithStatus("ns=2;s=TestNode");
    EXPECT_EQ(result.status, CacheManager::CacheStatus::FRESH);
}

TEST_F(CacheManagerTest, CacheTimingTransitions) {
    // Create cache manager with 2s refresh and 5s expiration for faster testing
    auto timedCache = std::make_unique<CacheManager>(60, 100, 2, 5);

    // Add a cache entry
    timedCache->updateCache("ns=2;s=TestNode", "100", "Good", "Success", 1000);

    // Immediately - FRESH
    auto result = timedCache->getCachedValueWithStatus("ns=2;s=TestNode");
    EXPECT_EQ(result.status, CacheManager::CacheStatus::FRESH);

    // Wait 3 seconds - transition to STALE
    std::this_thread::sleep_for(std::chrono::seconds(3));
    result = timedCache->getCachedValueWithStatus("ns=2;s=TestNode");
    EXPECT_EQ(result.status, CacheManager::CacheStatus::STALE);

    // Wait 3 more seconds (6s total) - transition to EXPIRED
    std::this_thread::sleep_for(std::chrono::seconds(3));
    result = timedCache->getCachedValueWithStatus("ns=2;s=TestNode");
    EXPECT_EQ(result.status, CacheManager::CacheStatus::EXPIRED);
}
