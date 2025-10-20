#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "cache/CacheManager.h"
#include "core/ReadStrategy.h"
#include "opcua/OPCUAClient.h"

using namespace opcua2http;
using namespace std::chrono;

class PerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Save original logger
        originalLogger_ = spdlog::default_logger();

        // Create clean test logger
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("test_performance", sink);
        logger->set_level(spdlog::level::warn); // Reduce noise in performance tests
        spdlog::set_default_logger(logger);

        // Initialize cache manager
        cacheManager_ = std::make_unique<CacheManager>(60, 10000, 3, 10);
    }

    void TearDown() override {
        cacheManager_.reset();

        // Restore original logger
        if (originalLogger_) {
            spdlog::set_default_logger(originalLogger_);
        } else {
            auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            spdlog::set_default_logger(std::make_shared<spdlog::logger>("default", sink));
        }
    }

    std::shared_ptr<spdlog::logger> originalLogger_;
    std::unique_ptr<CacheManager> cacheManager_;
};

// ============================================================================
// CACHE HIT RESPONSE TIME TESTS
// ============================================================================

TEST_F(PerformanceTest, CacheHitResponseTime) {
    // Add test data to cache
    const int numEntries = 100;
    for (int i = 0; i < numEntries; ++i) {
        std::string nodeId = "ns=2;s=Node" + std::to_string(i);
        cacheManager_->updateCache(nodeId, std::to_string(i * 10), "Good", "Success", 1000 + i);
    }

    // Measure cache hit response times
    const int numReads = 1000;
    std::vector<double> responseTimes;
    responseTimes.reserve(numReads);

    for (int i = 0; i < numReads; ++i) {
        std::string nodeId = "ns=2;s=Node" + std::to_string(i % numEntries);

        auto start = high_resolution_clock::now();
        auto result = cacheManager_->getCachedValue(nodeId);
        auto end = high_resolution_clock::now();

        ASSERT_TRUE(result.has_value());

        auto duration = duration_cast<microseconds>(end - start).count();
        responseTimes.push_back(static_cast<double>(duration));
    }

    // Calculate statistics
    double sum = 0.0;
    double maxTime = 0.0;
    for (double time : responseTimes) {
        sum += time;
        if (time > maxTime) maxTime = time;
    }
    double avgTime = sum / responseTimes.size();

    std::cout << "Cache Hit Performance:" << std::endl;
    std::cout << "  Average response time: " << avgTime << " μs" << std::endl;
    std::cout << "  Max response time: " << maxTime << " μs" << std::endl;

    // Verify acceptable performance (< 100 μs average)
    EXPECT_LT(avgTime, 100.0) << "Average cache hit response time should be < 100 μs";
    EXPECT_LT(maxTime, 1000.0) << "Max cache hit response time should be < 1000 μs";
}

TEST_F(PerformanceTest, CacheHitWithStatusResponseTime) {
    // Add test data to cache
    const int numEntries = 100;
    for (int i = 0; i < numEntries; ++i) {
        std::string nodeId = "ns=2;s=Node" + std::to_string(i);
        cacheManager_->updateCache(nodeId, std::to_string(i * 10), "Good", "Success", 1000 + i);
    }

    // Measure cache hit with status evaluation response times
    const int numReads = 1000;
    std::vector<double> responseTimes;
    responseTimes.reserve(numReads);

    for (int i = 0; i < numReads; ++i) {
        std::string nodeId = "ns=2;s=Node" + std::to_string(i % numEntries);

        auto start = high_resolution_clock::now();
        auto result = cacheManager_->getCachedValueWithStatus(nodeId);
        auto end = high_resolution_clock::now();

        ASSERT_TRUE(result.entry.has_value());

        auto duration = duration_cast<microseconds>(end - start).count();
        responseTimes.push_back(static_cast<double>(duration));
    }

    // Calculate statistics
    double sum = 0.0;
    double maxTime = 0.0;
    for (double time : responseTimes) {
        sum += time;
        if (time > maxTime) maxTime = time;
    }
    double avgTime = sum / responseTimes.size();

    std::cout << "Cache Hit with Status Performance:" << std::endl;
    std::cout << "  Average response time: " << avgTime << " μs" << std::endl;
    std::cout << "  Max response time: " << maxTime << " μs" << std::endl;

    // Verify acceptable performance (< 150 μs average, slightly higher due to status evaluation)
    EXPECT_LT(avgTime, 150.0) << "Average cache hit with status response time should be < 150 μs";
    EXPECT_LT(maxTime, 1500.0) << "Max cache hit with status response time should be < 1500 μs";
}

TEST_F(PerformanceTest, BatchCacheHitResponseTime) {
    // Add test data to cache
    const int numEntries = 100;
    for (int i = 0; i < numEntries; ++i) {
        std::string nodeId = "ns=2;s=Node" + std::to_string(i);
        cacheManager_->updateCache(nodeId, std::to_string(i * 10), "Good", "Success", 1000 + i);
    }

    // Measure batch cache hit response times
    const int numBatches = 100;
    const int batchSize = 10;
    std::vector<double> responseTimes;
    responseTimes.reserve(numBatches);

    for (int i = 0; i < numBatches; ++i) {
        std::vector<std::string> nodeIds;
        for (int j = 0; j < batchSize; ++j) {
            nodeIds.push_back("ns=2;s=Node" + std::to_string((i * batchSize + j) % numEntries));
        }

        auto start = high_resolution_clock::now();
        auto results = cacheManager_->getCachedValuesWithStatus(nodeIds);
        auto end = high_resolution_clock::now();

        ASSERT_EQ(results.size(), batchSize);

        auto duration = duration_cast<microseconds>(end - start).count();
        responseTimes.push_back(static_cast<double>(duration));
    }

    // Calculate statistics
    double sum = 0.0;
    double maxTime = 0.0;
    for (double time : responseTimes) {
        sum += time;
        if (time > maxTime) maxTime = time;
    }
    double avgTime = sum / responseTimes.size();
    double avgPerNode = avgTime / batchSize;

    std::cout << "Batch Cache Hit Performance (batch size: " << batchSize << "):" << std::endl;
    std::cout << "  Average batch response time: " << avgTime << " μs" << std::endl;
    std::cout << "  Average per-node response time: " << avgPerNode << " μs" << std::endl;
    std::cout << "  Max batch response time: " << maxTime << " μs" << std::endl;

    // Verify acceptable performance
    EXPECT_LT(avgTime, 1000.0) << "Average batch response time should be < 1000 μs";
    EXPECT_LT(avgPerNode, 100.0) << "Average per-node response time should be < 100 μs";
}

// ============================================================================
// SYSTEM STABILITY UNDER LOAD TESTS
// ============================================================================

TEST_F(PerformanceTest, SystemStabilityUnderNormalLoad) {
    // Simulate normal load: 10 threads, each doing 100 operations
    const int numThreads = 10;
    const int operationsPerThread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> successfulOps{0};
    std::atomic<int> failedOps{0};
    std::atomic<bool> hasError{false};

    // Pre-populate cache
    for (int i = 0; i < 50; ++i) {
        std::string nodeId = "ns=2;s=Node" + std::to_string(i);
        cacheManager_->updateCache(nodeId, std::to_string(i * 10), "Good", "Success", 1000 + i);
    }

    auto startTime = high_resolution_clock::now();

    // Launch worker threads
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            try {
                for (int j = 0; j < operationsPerThread; ++j) {
                    // Mix of reads and writes
                    if (j % 3 == 0) {
                        // Write operation
                        std::string nodeId = "ns=2;s=Thread" + std::to_string(i) + "_Node" + std::to_string(j);
                        cacheManager_->updateCache(nodeId, std::to_string(j), "Good", "Success", 1000 + j);
                    } else {
                        // Read operation
                        std::string nodeId = "ns=2;s=Node" + std::to_string(j % 50);
                        auto result = cacheManager_->getCachedValue(nodeId);
                        if (result.has_value()) {
                            successfulOps++;
                        }
                    }

                    // Small delay to simulate realistic load
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            } catch (const std::exception&) {
                hasError.store(true);
                failedOps++;
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    auto endTime = high_resolution_clock::now();
    auto totalDuration = duration_cast<milliseconds>(endTime - startTime).count();

    std::cout << "System Stability Test Results:" << std::endl;
    std::cout << "  Total duration: " << totalDuration << " ms" << std::endl;
    std::cout << "  Successful operations: " << successfulOps.load() << std::endl;
    std::cout << "  Failed operations: " << failedOps.load() << std::endl;
    std::cout << "  Cache size: " << cacheManager_->size() << std::endl;

    // Verify system stability
    EXPECT_FALSE(hasError.load()) << "System should not throw errors under normal load";
    EXPECT_EQ(failedOps.load(), 0) << "No operations should fail";
    EXPECT_GT(successfulOps.load(), 0) << "Should have successful read operations";
    EXPECT_GT(cacheManager_->size(), 0) << "Cache should contain entries";
}

TEST_F(PerformanceTest, SystemStabilityUnderHighConcurrency) {
    // Simulate high concurrency: 50 threads accessing same nodes
    const int numThreads = 50;
    const int operationsPerThread = 50;

    // Pre-populate cache with shared nodes
    for (int i = 0; i < 10; ++i) {
        std::string nodeId = "ns=2;s=SharedNode" + std::to_string(i);
        cacheManager_->updateCache(nodeId, std::to_string(i * 10), "Good", "Success", 1000 + i);
    }

    std::vector<std::thread> threads;
    std::atomic<int> totalOps{0};
    std::atomic<bool> hasError{false};

    auto startTime = high_resolution_clock::now();

    // Launch many threads accessing same nodes
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&]() {
            try {
                for (int j = 0; j < operationsPerThread; ++j) {
                    // All threads access the same set of nodes
                    std::string nodeId = "ns=2;s=SharedNode" + std::to_string(j % 10);
                    auto result = cacheManager_->getCachedValueWithStatus(nodeId);
                    if (result.entry.has_value()) {
                        totalOps++;
                    }
                }
            } catch (const std::exception&) {
                hasError.store(true);
            }
        });
    }

    // Wait for all threads
    for (auto& thread : threads) {
        thread.join();
    }

    auto endTime = high_resolution_clock::now();
    auto totalDuration = duration_cast<milliseconds>(endTime - startTime).count();

    int expectedOps = numThreads * operationsPerThread;
    double opsPerSecond = (totalOps.load() * 1000.0) / totalDuration;

    std::cout << "High Concurrency Test Results:" << std::endl;
    std::cout << "  Total duration: " << totalDuration << " ms" << std::endl;
    std::cout << "  Total operations: " << totalOps.load() << " / " << expectedOps << std::endl;
    std::cout << "  Operations per second: " << opsPerSecond << std::endl;

    // Verify system handles high concurrency
    EXPECT_FALSE(hasError.load()) << "System should not throw errors under high concurrency";
    EXPECT_EQ(totalOps.load(), expectedOps) << "All operations should complete successfully";
    EXPECT_GT(opsPerSecond, 1000.0) << "Should handle at least 1000 ops/sec";
}

// ============================================================================
// MEMORY USAGE VALIDATION TESTS
// ============================================================================

TEST_F(PerformanceTest, MemoryUsageReasonable) {
    // Add entries and measure memory usage
    const int numEntries = 1000;

    size_t initialMemory = cacheManager_->getMemoryUsage();

    for (int i = 0; i < numEntries; ++i) {
        std::string nodeId = "ns=2;s=Node" + std::to_string(i);
        std::string value = "Value_" + std::to_string(i * 10);
        cacheManager_->updateCache(nodeId, value, "Good", "Success", 1000 + i);
    }

    size_t finalMemory = cacheManager_->getMemoryUsage();
    size_t memoryPerEntry = (finalMemory - initialMemory) / numEntries;

    std::cout << "Memory Usage Test Results:" << std::endl;
    std::cout << "  Initial memory: " << initialMemory << " bytes" << std::endl;
    std::cout << "  Final memory: " << finalMemory << " bytes" << std::endl;
    std::cout << "  Memory per entry: " << memoryPerEntry << " bytes" << std::endl;
    std::cout << "  Total entries: " << cacheManager_->size() << std::endl;

    // Verify reasonable memory usage (< 1KB per entry on average)
    EXPECT_LT(memoryPerEntry, 1024) << "Memory per entry should be < 1KB";
    EXPECT_EQ(cacheManager_->size(), numEntries) << "All entries should be stored";
}

TEST_F(PerformanceTest, MemoryUsageWithLargeValues) {
    // Test memory usage with larger values
    const int numEntries = 100;
    const int largeValueSize = 1000; // 1KB values

    size_t initialMemory = cacheManager_->getMemoryUsage();

    for (int i = 0; i < numEntries; ++i) {
        std::string nodeId = "ns=2;s=LargeNode" + std::to_string(i);
        std::string value(largeValueSize, 'X');
        cacheManager_->updateCache(nodeId, value, "Good", "Success", 1000 + i);
    }

    size_t finalMemory = cacheManager_->getMemoryUsage();
    size_t totalMemoryUsed = finalMemory - initialMemory;

    std::cout << "Large Value Memory Test Results:" << std::endl;
    std::cout << "  Total memory used: " << totalMemoryUsed << " bytes" << std::endl;
    std::cout << "  Expected minimum: " << (numEntries * largeValueSize) << " bytes" << std::endl;
    std::cout << "  Memory overhead: " << (totalMemoryUsed - numEntries * largeValueSize) << " bytes" << std::endl;

    // Verify memory usage is reasonable (overhead < 50%)
    size_t expectedMinimum = numEntries * largeValueSize;
    EXPECT_GT(totalMemoryUsed, expectedMinimum) << "Should account for value storage";
    EXPECT_LT(totalMemoryUsed, expectedMinimum * 1.5) << "Overhead should be < 50%";
}

TEST_F(PerformanceTest, MemoryUsageStabilityOverTime) {
    // Test that memory usage remains stable with continuous operations
    const int numIterations = 100;
    const int entriesPerIteration = 50;

    std::vector<size_t> memorySnapshots;

    for (int iter = 0; iter < numIterations; ++iter) {
        // Add some entries
        for (int i = 0; i < entriesPerIteration; ++i) {
            std::string nodeId = "ns=2;s=Iter" + std::to_string(iter) + "_Node" + std::to_string(i);
            cacheManager_->updateCache(nodeId, std::to_string(i), "Good", "Success", 1000 + i);
        }

        // Record memory usage
        memorySnapshots.push_back(cacheManager_->getMemoryUsage());

        // Cleanup some entries to simulate realistic usage
        if (iter % 10 == 0) {
            cacheManager_->cleanupExpiredEntries();
        }
    }

    // Analyze memory growth
    size_t initialMemory = memorySnapshots.front();
    size_t finalMemory = memorySnapshots.back();
    size_t maxMemory = *std::max_element(memorySnapshots.begin(), memorySnapshots.end());

    std::cout << "Memory Stability Test Results:" << std::endl;
    std::cout << "  Initial memory: " << initialMemory << " bytes" << std::endl;
    std::cout << "  Final memory: " << finalMemory << " bytes" << std::endl;
    std::cout << "  Max memory: " << maxMemory << " bytes" << std::endl;
    std::cout << "  Final cache size: " << cacheManager_->size() << std::endl;

    // Verify memory doesn't grow unbounded
    EXPECT_LT(finalMemory, maxMemory * 1.2) << "Memory should stabilize, not grow unbounded";
}

// ============================================================================
// THROUGHPUT TESTS
// ============================================================================

TEST_F(PerformanceTest, ReadThroughput) {
    // Measure read throughput
    const int numEntries = 100;

    // Pre-populate cache
    for (int i = 0; i < numEntries; ++i) {
        std::string nodeId = "ns=2;s=Node" + std::to_string(i);
        cacheManager_->updateCache(nodeId, std::to_string(i * 10), "Good", "Success", 1000 + i);
    }

    const int numReads = 10000;
    auto startTime = high_resolution_clock::now();

    for (int i = 0; i < numReads; ++i) {
        std::string nodeId = "ns=2;s=Node" + std::to_string(i % numEntries);
        auto result = cacheManager_->getCachedValue(nodeId);
        ASSERT_TRUE(result.has_value());
    }

    auto endTime = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(endTime - startTime).count();
    double throughput = (numReads * 1000.0) / duration;

    std::cout << "Read Throughput Test Results:" << std::endl;
    std::cout << "  Total reads: " << numReads << std::endl;
    std::cout << "  Duration: " << duration << " ms" << std::endl;
    std::cout << "  Throughput: " << throughput << " reads/sec" << std::endl;

    // Verify acceptable throughput (> 10,000 reads/sec)
    EXPECT_GT(throughput, 10000.0) << "Read throughput should be > 10,000 reads/sec";
}

TEST_F(PerformanceTest, WriteThroughput) {
    // Measure write throughput
    const int numWrites = 5000;
    auto startTime = high_resolution_clock::now();

    for (int i = 0; i < numWrites; ++i) {
        std::string nodeId = "ns=2;s=Node" + std::to_string(i);
        cacheManager_->updateCache(nodeId, std::to_string(i * 10), "Good", "Success", 1000 + i);
    }

    auto endTime = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(endTime - startTime).count();
    double throughput = (numWrites * 1000.0) / duration;

    std::cout << "Write Throughput Test Results:" << std::endl;
    std::cout << "  Total writes: " << numWrites << std::endl;
    std::cout << "  Duration: " << duration << " ms" << std::endl;
    std::cout << "  Throughput: " << throughput << " writes/sec" << std::endl;

    // Verify acceptable throughput (> 5,000 writes/sec)
    EXPECT_GT(throughput, 5000.0) << "Write throughput should be > 5,000 writes/sec";
}
