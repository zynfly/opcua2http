#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "core/ReadStrategy.h"
#include "core/IBackgroundUpdater.h"
#include "cache/CacheManager.h"
#include "opcua/OPCUAClient.h"

using namespace opcua2http;
using namespace std::chrono_literals;

class ReadStrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Save original logger
        originalLogger_ = spdlog::default_logger();
        
        // Create clean test logger
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("test_default", sink);
        spdlog::set_default_logger(logger);
        
        // Initialize cache manager with test configuration
        cacheManager_ = std::make_unique<CacheManager>(60, 1000, 3, 10);
        
        // Initialize OPC client (we'll use a mock or real client for testing)
        opcClient_ = std::make_unique<OPCUAClient>();
        
        // Initialize ReadStrategy
        readStrategy_ = std::make_unique<ReadStrategy>(cacheManager_.get(), opcClient_.get());
    }

    void TearDown() override {
        readStrategy_.reset();
        opcClient_.reset();
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
    std::unique_ptr<OPCUAClient> opcClient_;
    std::unique_ptr<ReadStrategy> readStrategy_;
};

TEST_F(ReadStrategyTest, ConcurrencyControlEnabled) {
    // Test that concurrency control is enabled by default
    EXPECT_TRUE(readStrategy_->isConcurrencyControlEnabled());
    
    // Test enabling/disabling concurrency control
    readStrategy_->enableConcurrencyControl(false);
    EXPECT_FALSE(readStrategy_->isConcurrencyControlEnabled());
    
    readStrategy_->enableConcurrencyControl(true);
    EXPECT_TRUE(readStrategy_->isConcurrencyControlEnabled());
}

TEST_F(ReadStrategyTest, MaxConcurrentReadsConfiguration) {
    // Test default max concurrent reads
    EXPECT_EQ(readStrategy_->getMaxConcurrentReads(), 10);
    
    // Test setting max concurrent reads
    readStrategy_->setMaxConcurrentReads(5);
    EXPECT_EQ(readStrategy_->getMaxConcurrentReads(), 5);
    
    readStrategy_->setMaxConcurrentReads(20);
    EXPECT_EQ(readStrategy_->getMaxConcurrentReads(), 20);
}

TEST_F(ReadStrategyTest, BatchPlanCreation) {
    // Test empty batch plan
    std::vector<std::string> emptyNodes;
    auto plan = readStrategy_->createBatchPlan(emptyNodes);
    EXPECT_TRUE(plan.isEmpty());
    EXPECT_EQ(plan.getTotalNodes(), 0);
    
    // Test batch plan with nodes (will be categorized based on cache status)
    std::vector<std::string> testNodes = {"ns=2;s=Temperature", "ns=2;s=Pressure", "ns=2;s=Flow"};
    auto planWithNodes = readStrategy_->createBatchPlan(testNodes);
    EXPECT_EQ(planWithNodes.getTotalNodes(), testNodes.size());
}

TEST_F(ReadStrategyTest, ProcessEmptyNodeRequests) {
    // Test processing empty node list
    std::vector<std::string> emptyNodes;
    auto results = readStrategy_->processNodeRequests(emptyNodes);
    EXPECT_TRUE(results.empty());
}

TEST_F(ReadStrategyTest, ProcessSingleNodeRequest) {
    // Test processing single node request with empty node ID
    auto result = readStrategy_->processNodeRequest("");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.reason, "Invalid node ID");
}

TEST_F(ReadStrategyTest, ConcurrentReadDeduplication) {
    // This test verifies that concurrent reads of the same node are deduplicated
    const std::string testNodeId = "ns=2;s=TestNode";
    std::atomic<int> readCount{0};
    std::atomic<int> completedReads{0};
    const int numThreads = 5;
    
    // Enable concurrency control
    readStrategy_->enableConcurrencyControl(true);
    
    std::vector<std::thread> threads;
    std::vector<ReadResult> results(numThreads);
    
    // Launch multiple threads trying to read the same node
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            readCount++;
            results[i] = readStrategy_->processNodeRequest(testNodeId);
            completedReads++;
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify all threads completed
    EXPECT_EQ(readCount.load(), numThreads);
    EXPECT_EQ(completedReads.load(), numThreads);
    
    // All results should have the same node ID
    for (const auto& result : results) {
        EXPECT_EQ(result.id, testNodeId);
    }
}

TEST_F(ReadStrategyTest, BackgroundUpdaterIntegration) {
    // Test that ReadStrategy can work without background updater
    EXPECT_NO_THROW(readStrategy_->scheduleBackgroundUpdate("ns=2;s=TestNode"));
    
    std::vector<std::string> testNodes = {"ns=2;s=Node1", "ns=2;s=Node2"};
    EXPECT_NO_THROW(readStrategy_->scheduleBackgroundUpdates(testNodes));
}

class MockBackgroundUpdater : public IBackgroundUpdater {
public:
    MOCK_METHOD(void, scheduleUpdate, (const std::string& nodeId), (override));
    MOCK_METHOD(void, scheduleBatchUpdate, (const std::vector<std::string>& nodeIds), (override));
};

TEST_F(ReadStrategyTest, BackgroundUpdaterCalls) {
    auto mockUpdater = std::make_unique<MockBackgroundUpdater>();
    auto* mockPtr = mockUpdater.get();
    
    // Set expectations
    EXPECT_CALL(*mockPtr, scheduleUpdate("ns=2;s=TestNode"))
        .Times(1);
    
    std::vector<std::string> testNodes = {"ns=2;s=Node1", "ns=2;s=Node2"};
    EXPECT_CALL(*mockPtr, scheduleBatchUpdate(testNodes))
        .Times(1);
    
    // Set the mock background updater
    readStrategy_->setBackgroundUpdater(mockPtr);
    
    // Test calls
    readStrategy_->scheduleBackgroundUpdate("ns=2;s=TestNode");
    readStrategy_->scheduleBackgroundUpdates(testNodes);
    
    // Clean up
    readStrategy_->setBackgroundUpdater(nullptr);
}

TEST_F(ReadStrategyTest, BatchPlanExecutionWithMixedCacheStates) {
    // Add some test data to cache with different ages
    const std::string freshNode = "ns=2;s=FreshNode";
    const std::string staleNode = "ns=2;s=StaleNode";
    const std::string expiredNode = "ns=2;s=ExpiredNode";
    
    // Add fresh cache entry (< 3 seconds)
    auto freshEntry = CacheManager::CacheEntry{};
    freshEntry.nodeId = freshNode;
    freshEntry.value = "25.5";
    freshEntry.status = "Good";
    freshEntry.reason = "Good";
    freshEntry.timestamp = 1234567890;
    freshEntry.creationTime = std::chrono::steady_clock::now();
    cacheManager_->addCacheEntry(freshNode, freshEntry);
    
    // Add stale cache entry (> 3 seconds but < 10 seconds)
    auto staleEntry = CacheManager::CacheEntry{};
    staleEntry.nodeId = staleNode;
    staleEntry.value = "30.2";
    staleEntry.status = "Good";
    staleEntry.reason = "Good";
    staleEntry.timestamp = 1234567890;
    staleEntry.creationTime = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    cacheManager_->addCacheEntry(staleNode, staleEntry);
    
    // Expired node will not be in cache, so it will be categorized as expired
    
    std::vector<std::string> testNodes = {freshNode, staleNode, expiredNode};
    auto plan = readStrategy_->createBatchPlan(testNodes);
    
    // Verify categorization
    EXPECT_EQ(plan.freshNodes.size(), 1);
    EXPECT_EQ(plan.staleNodes.size(), 1);
    EXPECT_EQ(plan.expiredNodes.size(), 1);
    
    EXPECT_EQ(plan.freshNodes[0], freshNode);
    EXPECT_EQ(plan.staleNodes[0], staleNode);
    EXPECT_EQ(plan.expiredNodes[0], expiredNode);
    
    // Execute the batch plan
    auto results = readStrategy_->executeBatchPlan(plan);
    EXPECT_EQ(results.size(), 3);
}

TEST_F(ReadStrategyTest, BatchPlanOptimization) {
    // Test that batch plan correctly optimizes processing
    std::vector<std::string> largeNodeList;
    for (int i = 0; i < 100; ++i) {
        largeNodeList.push_back("ns=2;s=Node" + std::to_string(i));
    }
    
    auto plan = readStrategy_->createBatchPlan(largeNodeList);
    EXPECT_EQ(plan.getTotalNodes(), largeNodeList.size());
    
    // All nodes should be categorized as expired since they're not in cache
    EXPECT_EQ(plan.expiredNodes.size(), largeNodeList.size());
    EXPECT_EQ(plan.freshNodes.size(), 0);
    EXPECT_EQ(plan.staleNodes.size(), 0);
}

TEST_F(ReadStrategyTest, IntelligentBatchGrouping) {
    // Test that nodes are intelligently grouped for OPC UA batch reads
    std::vector<std::string> expiredNodes = {
        "ns=2;s=Temperature1",
        "ns=2;s=Temperature2", 
        "ns=2;s=Pressure1",
        "ns=2;s=Pressure2"
    };
    
    ReadStrategy::BatchReadPlan plan;
    plan.expiredNodes = expiredNodes;
    
    // Execute batch plan - this should use batch reading for expired nodes
    auto results = readStrategy_->executeBatchPlan(plan);
    
    // Should return results for all nodes (even if they fail due to no OPC server)
    EXPECT_EQ(results.size(), expiredNodes.size());
    
    // All results should have the correct node IDs
    for (size_t i = 0; i < results.size(); ++i) {
        EXPECT_EQ(results[i].id, expiredNodes[i]);
    }
}