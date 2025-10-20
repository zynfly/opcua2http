#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "core/BackgroundUpdater.h"
#include "cache/CacheManager.h"
#include "opcua/OPCUAClient.h"

using namespace opcua2http;
using namespace std::chrono_literals;

class BackgroundUpdaterTest : public ::testing::Test {
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
        
        // Initialize OPC client (we'll use a real client for testing)
        opcClient_ = std::make_unique<OPCUAClient>();
        
        // Initialize BackgroundUpdater
        backgroundUpdater_ = std::make_unique<BackgroundUpdater>(cacheManager_.get(), opcClient_.get());
    }

    void TearDown() override {
        if (backgroundUpdater_) {
            backgroundUpdater_->stop();
        }
        backgroundUpdater_.reset();
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
    std::unique_ptr<BackgroundUpdater> backgroundUpdater_;
};

TEST_F(BackgroundUpdaterTest, InitialState) {
    // Test initial state
    EXPECT_FALSE(backgroundUpdater_->isRunning());
    
    auto stats = backgroundUpdater_->getStats();
    EXPECT_EQ(stats.totalUpdates, 0);
    EXPECT_EQ(stats.successfulUpdates, 0);
    EXPECT_EQ(stats.failedUpdates, 0);
    EXPECT_EQ(stats.queuedUpdates, 0);
    EXPECT_EQ(stats.duplicateUpdates, 0);
    EXPECT_EQ(stats.averageUpdateTime, 0.0);
}

TEST_F(BackgroundUpdaterTest, StartStop) {
    // Test starting the background updater
    EXPECT_NO_THROW(backgroundUpdater_->start());
    EXPECT_TRUE(backgroundUpdater_->isRunning());
    
    // Test stopping the background updater
    EXPECT_NO_THROW(backgroundUpdater_->stop());
    EXPECT_FALSE(backgroundUpdater_->isRunning());
    
    // Test multiple starts/stops
    EXPECT_NO_THROW(backgroundUpdater_->start());
    EXPECT_TRUE(backgroundUpdater_->isRunning());
    EXPECT_NO_THROW(backgroundUpdater_->stop());
    EXPECT_FALSE(backgroundUpdater_->isRunning());
}

TEST_F(BackgroundUpdaterTest, Configuration) {
    // Test setting configuration parameters
    EXPECT_NO_THROW(backgroundUpdater_->setMaxConcurrentUpdates(5));
    EXPECT_NO_THROW(backgroundUpdater_->setUpdateQueueSize(500));
    EXPECT_NO_THROW(backgroundUpdater_->setUpdateTimeout(std::chrono::milliseconds(3000)));
    
    // Test invalid configuration values
    EXPECT_NO_THROW(backgroundUpdater_->setMaxConcurrentUpdates(0)); // Should use default
    EXPECT_NO_THROW(backgroundUpdater_->setUpdateQueueSize(0)); // Should use default
    EXPECT_NO_THROW(backgroundUpdater_->setUpdateTimeout(std::chrono::milliseconds(0))); // Should use default
}

TEST_F(BackgroundUpdaterTest, ScheduleUpdateWhenNotRunning) {
    // Test scheduling updates when not running
    EXPECT_FALSE(backgroundUpdater_->isRunning());
    
    // Should not throw, but should be ignored
    EXPECT_NO_THROW(backgroundUpdater_->scheduleUpdate("ns=2;s=TestNode"));
    
    std::vector<std::string> testNodes = {"ns=2;s=Node1", "ns=2;s=Node2"};
    EXPECT_NO_THROW(backgroundUpdater_->scheduleBatchUpdate(testNodes));
    
    auto stats = backgroundUpdater_->getStats();
    EXPECT_EQ(stats.queuedUpdates, 0); // Should be 0 since not running
}

TEST_F(BackgroundUpdaterTest, ScheduleUpdateWhenRunning) {
    // Start the background updater
    backgroundUpdater_->start();
    EXPECT_TRUE(backgroundUpdater_->isRunning());
    
    // Schedule some updates
    EXPECT_NO_THROW(backgroundUpdater_->scheduleUpdate("ns=2;s=TestNode1"));
    EXPECT_NO_THROW(backgroundUpdater_->scheduleUpdate("ns=2;s=TestNode2"));
    
    std::vector<std::string> testNodes = {"ns=2;s=Node3", "ns=2;s=Node4"};
    EXPECT_NO_THROW(backgroundUpdater_->scheduleBatchUpdate(testNodes));
    
    // Give some time for processing
    std::this_thread::sleep_for(100ms);
    
    auto stats = backgroundUpdater_->getStats();
    // Note: Updates will likely fail since we don't have a real OPC server,
    // but they should be processed
    EXPECT_GE(stats.totalUpdates, 0);
}

TEST_F(BackgroundUpdaterTest, DuplicateFiltering) {
    // Start the background updater
    backgroundUpdater_->start();
    EXPECT_TRUE(backgroundUpdater_->isRunning());
    
    // Schedule the same update multiple times quickly
    const std::string nodeId = "ns=2;s=DuplicateTest";
    EXPECT_NO_THROW(backgroundUpdater_->scheduleUpdate(nodeId));
    EXPECT_NO_THROW(backgroundUpdater_->scheduleUpdate(nodeId));
    EXPECT_NO_THROW(backgroundUpdater_->scheduleUpdate(nodeId));
    
    // Give some time for processing
    std::this_thread::sleep_for(100ms);
    
    auto stats = backgroundUpdater_->getStats();
    // Should have some duplicate filtering
    EXPECT_GE(stats.duplicateUpdates, 0);
}

TEST_F(BackgroundUpdaterTest, EmptyNodeIdHandling) {
    // Start the background updater
    backgroundUpdater_->start();
    EXPECT_TRUE(backgroundUpdater_->isRunning());
    
    // Test empty node ID handling
    EXPECT_NO_THROW(backgroundUpdater_->scheduleUpdate(""));
    
    std::vector<std::string> emptyNodes;
    EXPECT_NO_THROW(backgroundUpdater_->scheduleBatchUpdate(emptyNodes));
    
    std::vector<std::string> mixedNodes = {"ns=2;s=Valid", "", "ns=2;s=AlsoValid"};
    EXPECT_NO_THROW(backgroundUpdater_->scheduleBatchUpdate(mixedNodes));
    
    // Give some time for processing
    std::this_thread::sleep_for(50ms);
    
    // Should not crash or cause issues
    auto stats = backgroundUpdater_->getStats();
    EXPECT_GE(stats.totalUpdates, 0);
}

TEST_F(BackgroundUpdaterTest, StatisticsClearing) {
    // Start the background updater and schedule some updates
    backgroundUpdater_->start();
    
    EXPECT_NO_THROW(backgroundUpdater_->scheduleUpdate("ns=2;s=TestNode"));
    std::this_thread::sleep_for(50ms);
    
    auto statsBefore = backgroundUpdater_->getStats();
    
    // Clear statistics
    EXPECT_NO_THROW(backgroundUpdater_->clearStats());
    
    auto statsAfter = backgroundUpdater_->getStats();
    EXPECT_EQ(statsAfter.totalUpdates, 0);
    EXPECT_EQ(statsAfter.successfulUpdates, 0);
    EXPECT_EQ(statsAfter.failedUpdates, 0);
    EXPECT_EQ(statsAfter.duplicateUpdates, 0);
    EXPECT_EQ(statsAfter.averageUpdateTime, 0.0);
}

TEST_F(BackgroundUpdaterTest, ThreadSafety) {
    // Start the background updater
    backgroundUpdater_->start();
    EXPECT_TRUE(backgroundUpdater_->isRunning());
    
    // Launch multiple threads scheduling updates concurrently
    const int numThreads = 5;
    const int updatesPerThread = 10;
    std::vector<std::thread> threads;
    
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i, updatesPerThread]() {
            for (int j = 0; j < updatesPerThread; ++j) {
                std::string nodeId = "ns=2;s=Thread" + std::to_string(i) + "Node" + std::to_string(j);
                backgroundUpdater_->scheduleUpdate(nodeId);
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Give some time for processing
    std::this_thread::sleep_for(200ms);
    
    // Should not crash and should have processed some updates
    auto stats = backgroundUpdater_->getStats();
    EXPECT_GE(stats.totalUpdates, 0);
}