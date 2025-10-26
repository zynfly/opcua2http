#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <memory>
#include <atomic>

#include "common/MockOPCUAServer.h"
#include "opcua/OPCUAClient.h"
#include "subscription/SubscriptionManager.h"
#include "reconnection/ReconnectionManager.h"
#include "cache/CacheManager.h"
#include "config/Configuration.h"

namespace opcua2http {
namespace test {

/**
 * @brief Integration tests for server restart reconnection functionality
 *
 * These tests verify that the ReconnectionManager correctly handles OPC UA
 * server restarts by detecting disconnections and automatically reconnecting.
 *
 * Key fix: monitoringLoop() now calls runIterate() to process network events,
 * enabling automatic detection of connection state changes.
 */
class ServerRestartReconnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use unique port for this test suite
        serverPort_ = 4845;

        // Create and start mock server
        mockServer_ = std::make_unique<MockOPCUAServer>(serverPort_, "http://test.reconnection.restart");
        mockServer_->addStandardTestVariables();

        ASSERT_TRUE(mockServer_->start()) << "Failed to start mock server";

        // Configure client
        config_.opcEndpoint = mockServer_->getEndpoint();
        config_.securityMode = 1; // None
        config_.securityPolicy = "None";
        config_.defaultNamespace = mockServer_->getTestNamespaceIndex();
        config_.applicationUri = "urn:test:opcua:reconnection:restart:client";

        // Reconnection configuration - use realistic delays
        config_.connectionRetryMax = 3;
        config_.connectionInitialDelay = 100;   // 100ms
        config_.connectionMaxRetry = 5;
        config_.connectionMaxDelay = 2000;      // 2 seconds
        config_.connectionRetryDelay = 500;     // 500ms

        // Create components
        opcClient_ = std::make_unique<OPCUAClient>();
        cacheManager_ = std::make_unique<CacheManager>(60, 1000);

        // Initialize and connect client
        ASSERT_TRUE(opcClient_->initialize(config_)) << "Failed to initialize OPC UA client";
        ASSERT_TRUE(opcClient_->connect()) << "Failed to connect to OPC UA server";

        // Create subscription manager
        subscriptionManager_ = std::make_unique<SubscriptionManager>(
            opcClient_.get(), cacheManager_.get(), 1);

        // Create reconnection manager
        reconnectionManager_ = std::make_unique<ReconnectionManager>(
            opcClient_.get(), subscriptionManager_.get(), config_);
    }

    void TearDown() override {
        // Clean up in reverse order
        if (reconnectionManager_) {
            reconnectionManager_->stopMonitoring();
            reconnectionManager_.reset();
        }

        if (subscriptionManager_) {
            subscriptionManager_.reset();
        }

        if (opcClient_) {
            if (opcClient_->isConnected()) {
                opcClient_->disconnect();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            opcClient_.reset();
        }

        if (cacheManager_) {
            cacheManager_.reset();
        }

        if (mockServer_) {
            mockServer_->stop();
            mockServer_.reset();
        }
    }

    std::string getTestNodeId(UA_UInt32 nodeId) const {
        return "ns=" + std::to_string(config_.defaultNamespace) + ";i=" + std::to_string(nodeId);
    }

    bool waitForDisconnection(std::chrono::milliseconds timeout) {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout) {
            if (!opcClient_->isConnected() ||
                reconnectionManager_->getState() == ReconnectionManager::ReconnectionState::RECONNECTING) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    bool waitForReconnection(std::chrono::milliseconds timeout) {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout) {
            if (opcClient_->isConnected()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    uint16_t serverPort_;
    std::unique_ptr<MockOPCUAServer> mockServer_;
    std::unique_ptr<OPCUAClient> opcClient_;
    std::unique_ptr<CacheManager> cacheManager_;
    std::unique_ptr<SubscriptionManager> subscriptionManager_;
    std::unique_ptr<ReconnectionManager> reconnectionManager_;
    Configuration config_;
};

/**
 * @brief Test basic server restart reconnection
 *
 * Verifies Requirements: 1.1, 1.2, 1.3, 2.1, 2.2, 2.3
 *
 * Test flow:
 * 1. Connect to server with monitoring enabled
 * 2. Stop server (simulate shutdown)
 * 3. Verify disconnection detected within 10 seconds
 * 4. Restart server
 * 5. Verify reconnection within 10 seconds
 */
TEST_F(ServerRestartReconnectionTest, BasicServerRestart) {
    std::cout << "\n=== Test: Basic Server Restart Reconnection ===" << std::endl;

    // Step 1: Start monitoring
    std::cout << "Step 1: Starting reconnection monitoring..." << std::endl;
    ASSERT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_TRUE(reconnectionManager_->isMonitoring());
    std::cout << "  ✓ Monitoring started" << std::endl;

    // Step 2: Verify initial connection
    std::cout << "Step 2: Verifying initial connection..." << std::endl;
    ASSERT_TRUE(opcClient_->isConnected());
    std::cout << "  ✓ Connected" << std::endl;

    // Step 3: Stop server
    std::cout << "Step 3: Stopping server..." << std::endl;
    auto shutdownTime = std::chrono::steady_clock::now();
    mockServer_->stop();
    ASSERT_FALSE(mockServer_->isRunning());
    std::cout << "  ✓ Server stopped" << std::endl;

    // Step 4: Wait for disconnection detection (Requirement 1.1)
    std::cout << "Step 4: Waiting for disconnection detection..." << std::endl;
    bool disconnected = waitForDisconnection(std::chrono::seconds(10));

    ASSERT_TRUE(disconnected) << "Should detect disconnection within 10 seconds";

    auto disconnectionTime = std::chrono::steady_clock::now();
    auto detectionDelay = std::chrono::duration_cast<std::chrono::milliseconds>(
        disconnectionTime - shutdownTime);
    std::cout << "  ✓ Disconnection detected after " << detectionDelay.count() << "ms" << std::endl;

    // Step 5: Restart server (Requirement 2.2)
    std::cout << "Step 5: Restarting server..." << std::endl;
    auto restartTime = std::chrono::steady_clock::now();
    ASSERT_TRUE(mockServer_->restart());
    ASSERT_TRUE(mockServer_->isRunning());
    std::cout << "  ✓ Server restarted" << std::endl;

    // Step 6: Wait for reconnection (Requirement 1.3)
    std::cout << "Step 6: Waiting for reconnection..." << std::endl;
    bool reconnected = waitForReconnection(std::chrono::seconds(10));

    ASSERT_TRUE(reconnected) << "Should reconnect within 10 seconds";

    auto reconnectionTime = std::chrono::steady_clock::now();
    auto reconnectionDelay = std::chrono::duration_cast<std::chrono::milliseconds>(
        reconnectionTime - restartTime);
    std::cout << "  ✓ Reconnected after " << reconnectionDelay.count() << "ms" << std::endl;

    // Verify statistics
    auto stats = reconnectionManager_->getStats();
    std::cout << "\nReconnection Statistics:" << std::endl;
    std::cout << "  - Total attempts: " << stats.totalReconnectionAttempts << std::endl;
    std::cout << "  - Successful: " << stats.successfulReconnections << std::endl;
    std::cout << "  - Failed: " << stats.failedReconnections << std::endl;

    EXPECT_GE(stats.successfulReconnections, 1) << "Should have at least one successful reconnection";

    std::cout << "=== Test Complete: PASSED ===" << std::endl;
}

/**
 * @brief Test multiple server restart cycles
 *
 * Verifies Requirement 1.5: The system should handle multiple server restarts
 * without manual intervention.
 *
 * Test flow:
 * - Perform 3 server restart cycles
 * - Verify successful reconnection after each restart
 */
TEST_F(ServerRestartReconnectionTest, MultipleServerRestarts) {
    std::cout << "\n=== Test: Multiple Server Restart Cycles ===" << std::endl;

    // Start monitoring
    ASSERT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const int numCycles = 3;
    int successfulReconnections = 0;

    for (int cycle = 1; cycle <= numCycles; ++cycle) {
        std::cout << "\n--- Restart Cycle " << cycle << " of " << numCycles << " ---" << std::endl;

        // Verify connected
        ASSERT_TRUE(opcClient_->isConnected())
            << "Should be connected at start of cycle " << cycle;

        // Stop server
        std::cout << "Stopping server..." << std::endl;
        mockServer_->stop();

        // Wait for disconnection
        bool disconnected = waitForDisconnection(std::chrono::seconds(10));
        EXPECT_TRUE(disconnected) << "Should detect disconnection in cycle " << cycle;

        // Restart server
        std::cout << "Restarting server..." << std::endl;
        ASSERT_TRUE(mockServer_->restart());

        // Wait for reconnection
        bool reconnected = waitForReconnection(std::chrono::seconds(10));

        if (reconnected) {
            successfulReconnections++;
            std::cout << "✓ Reconnected in cycle " << cycle << std::endl;
        } else {
            std::cout << "✗ Failed to reconnect in cycle " << cycle << std::endl;
        }

        // Small delay between cycles
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\n=== Multiple Restart Test Results ===" << std::endl;
    std::cout << "Successful reconnections: " << successfulReconnections
              << " out of " << numCycles << std::endl;

    // Requirement 1.5: Should reconnect successfully in all cycles
    EXPECT_EQ(successfulReconnections, numCycles)
        << "Should successfully reconnect after each server restart";

    std::cout << "=== Test Complete ===" << std::endl;
}

/**
 * @brief Test server restart with subscription recovery
 *
 * Verifies Requirement 1.4: Subscriptions should be automatically restored
 * after reconnection.
 *
 * Test flow:
 * 1. Create subscriptions
 * 2. Restart server
 * 3. Verify subscriptions are restored
 */
TEST_F(ServerRestartReconnectionTest, SubscriptionRecoveryAfterRestart) {
    std::cout << "\n=== Test: Subscription Recovery After Restart ===" << std::endl;

    // Step 1: Add subscriptions
    std::cout << "Step 1: Adding subscriptions..." << std::endl;
    std::string nodeId1 = getTestNodeId(1001);
    std::string nodeId2 = getTestNodeId(1002);

    ASSERT_TRUE(subscriptionManager_->addMonitoredItem(nodeId1));
    ASSERT_TRUE(subscriptionManager_->addMonitoredItem(nodeId2));

    auto initialSubscriptions = subscriptionManager_->getActiveMonitoredItems();
    ASSERT_EQ(initialSubscriptions.size(), 2) << "Should have 2 active subscriptions";
    std::cout << "  ✓ Added " << initialSubscriptions.size() << " subscriptions" << std::endl;

    // Step 2: Start monitoring
    std::cout << "Step 2: Starting monitoring..." << std::endl;
    ASSERT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Step 3: Restart server
    std::cout << "Step 3: Restarting server..." << std::endl;
    mockServer_->stop();

    bool disconnected = waitForDisconnection(std::chrono::seconds(10));
    ASSERT_TRUE(disconnected);
    std::cout << "  ✓ Disconnection detected" << std::endl;

    ASSERT_TRUE(mockServer_->restart());
    std::cout << "  ✓ Server restarted" << std::endl;

    // Step 4: Wait for reconnection
    std::cout << "Step 4: Waiting for reconnection..." << std::endl;
    bool reconnected = waitForReconnection(std::chrono::seconds(10));
    ASSERT_TRUE(reconnected);
    std::cout << "  ✓ Reconnected" << std::endl;

    // Step 5: Verify subscription recovery (Requirement 1.4)
    std::cout << "Step 5: Verifying subscription recovery..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Allow time for recovery

    auto restoredSubscriptions = subscriptionManager_->getActiveMonitoredItems();
    std::cout << "  - Initial subscriptions: " << initialSubscriptions.size() << std::endl;
    std::cout << "  - Restored subscriptions: " << restoredSubscriptions.size() << std::endl;

    EXPECT_EQ(restoredSubscriptions.size(), initialSubscriptions.size())
        << "All subscriptions should be restored after reconnection";

    // Verify statistics
    auto stats = reconnectionManager_->getStats();
    std::cout << "\nRecovery Statistics:" << std::endl;
    std::cout << "  - Subscription recoveries: " << stats.subscriptionRecoveries << std::endl;
    std::cout << "  - Successful recoveries: " << stats.successfulSubscriptionRecoveries << std::endl;

    EXPECT_GE(stats.subscriptionRecoveries, 1) << "Should have attempted subscription recovery";

    std::cout << "=== Test Complete: PASSED ===" << std::endl;
}

/**
 * @brief Test reconnection with short server downtime
 *
 * Verifies that reconnection works quickly when server comes back up
 * within a few seconds.
 */
TEST_F(ServerRestartReconnectionTest, ShortDowntimeReconnection) {
    std::cout << "\n=== Test: Short Downtime Reconnection ===" << std::endl;

    ASSERT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "Stopping server..." << std::endl;
    mockServer_->stop();

    // Wait for disconnection
    bool disconnected = waitForDisconnection(std::chrono::seconds(10));
    ASSERT_TRUE(disconnected);
    std::cout << "  ✓ Disconnection detected" << std::endl;

    // Short delay before restart (< 2 seconds)
    std::cout << "Waiting 1 second before restart..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "Restarting server..." << std::endl;
    auto restartTime = std::chrono::steady_clock::now();
    ASSERT_TRUE(mockServer_->restart());

    // Should reconnect quickly
    bool reconnected = waitForReconnection(std::chrono::seconds(5));
    ASSERT_TRUE(reconnected) << "Should reconnect within 5 seconds for short downtime";

    auto reconnectionTime = std::chrono::steady_clock::now();
    auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
        reconnectionTime - restartTime);

    std::cout << "  ✓ Reconnected after " << delay.count() << "ms" << std::endl;

    // For short downtime, reconnection should be relatively quick
    EXPECT_LE(delay.count(), 5000) << "Reconnection should be quick for short downtime";

    std::cout << "=== Test Complete: PASSED ===" << std::endl;
}

/**
 * @brief Test reconnection with long server downtime
 *
 * Verifies that reconnection works correctly when server is down for
 * an extended period (> 10 seconds). This tests the extended retry
 * mechanism that continues attempting reconnection beyond the initial
 * max retry count.
 *
 * Verifies Requirements: 1.1, 1.2, 1.3, 2.1, 2.2, 2.3
 */
TEST_F(ServerRestartReconnectionTest, LongDowntimeReconnection) {
    std::cout << "\n=== Test: Long Downtime Reconnection ===" << std::endl;

    ASSERT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "Stopping server..." << std::endl;
    auto shutdownTime = std::chrono::steady_clock::now();
    mockServer_->stop();

    // Wait for disconnection
    bool disconnected = waitForDisconnection(std::chrono::seconds(10));
    ASSERT_TRUE(disconnected);
    std::cout << "  ✓ Disconnection detected" << std::endl;

    // Long delay before restart (> 10 seconds)
    std::cout << "Waiting 12 seconds before restart (simulating extended downtime)..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(12));

    std::cout << "Restarting server..." << std::endl;
    auto restartTime = std::chrono::steady_clock::now();
    ASSERT_TRUE(mockServer_->restart());

    // Should still reconnect despite long downtime
    std::cout << "Waiting for reconnection..." << std::endl;
    bool reconnected = waitForReconnection(std::chrono::seconds(15));
    ASSERT_TRUE(reconnected) << "Should reconnect even after extended downtime";

    auto reconnectionTime = std::chrono::steady_clock::now();
    auto totalDowntime = std::chrono::duration_cast<std::chrono::milliseconds>(
        reconnectionTime - shutdownTime);
    auto reconnectionDelay = std::chrono::duration_cast<std::chrono::milliseconds>(
        reconnectionTime - restartTime);

    std::cout << "  ✓ Reconnected after server restart" << std::endl;
    std::cout << "  - Total downtime: " << totalDowntime.count() << "ms" << std::endl;
    std::cout << "  - Reconnection delay: " << reconnectionDelay.count() << "ms" << std::endl;

    // Verify statistics show multiple retry attempts
    auto stats = reconnectionManager_->getStats();
    std::cout << "\nReconnection Statistics:" << std::endl;
    std::cout << "  - Total attempts: " << stats.totalReconnectionAttempts << std::endl;
    std::cout << "  - Successful: " << stats.successfulReconnections << std::endl;
    std::cout << "  - Failed: " << stats.failedReconnections << std::endl;

    // With long downtime, we expect multiple failed attempts before success
    // The system should make at least 3 attempts during the 12+ second downtime
    EXPECT_GE(stats.totalReconnectionAttempts, 3)
        << "Should have made multiple reconnection attempts during long downtime";

    EXPECT_GE(stats.successfulReconnections, 1)
        << "Should have at least one successful reconnection";

    // Verify that the system continued trying despite the long downtime
    EXPECT_GT(stats.failedReconnections, 0)
        << "Should have some failed attempts before successful reconnection";

    std::cout << "=== Test Complete: PASSED ===" << std::endl;
}

/**
 * @brief Test reconnection statistics accuracy
 *
 * Verifies that ReconnectionManager correctly tracks statistics during
 * reconnection attempts.
 */
TEST_F(ServerRestartReconnectionTest, ReconnectionStatisticsAccuracy) {
    std::cout << "\n=== Test: Reconnection Statistics Accuracy ===" << std::endl;

    ASSERT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Get initial stats
    auto initialStats = reconnectionManager_->getStats();
    std::cout << "Initial stats:" << std::endl;
    std::cout << "  - Total attempts: " << initialStats.totalReconnectionAttempts << std::endl;
    std::cout << "  - Successful: " << initialStats.successfulReconnections << std::endl;

    // Perform restart
    mockServer_->stop();
    bool disconnected = waitForDisconnection(std::chrono::seconds(10));
    ASSERT_TRUE(disconnected);

    ASSERT_TRUE(mockServer_->restart());
    bool reconnected = waitForReconnection(std::chrono::seconds(10));
    ASSERT_TRUE(reconnected);

    // Get final stats
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto finalStats = reconnectionManager_->getStats();

    std::cout << "\nFinal stats:" << std::endl;
    std::cout << "  - Total attempts: " << finalStats.totalReconnectionAttempts << std::endl;
    std::cout << "  - Successful: " << finalStats.successfulReconnections << std::endl;
    std::cout << "  - Failed: " << finalStats.failedReconnections << std::endl;
    std::cout << "  - Total downtime: " << finalStats.totalDowntime.count() << "ms" << std::endl;

    // Verify statistics
    EXPECT_GT(finalStats.totalReconnectionAttempts, initialStats.totalReconnectionAttempts)
        << "Should have more reconnection attempts";

    EXPECT_GT(finalStats.successfulReconnections, initialStats.successfulReconnections)
        << "Should have more successful reconnections";

    EXPECT_GT(finalStats.totalDowntime.count(), 0)
        << "Should have recorded downtime";

    std::cout << "=== Test Complete: PASSED ===" << std::endl;
}

// ============================================================================
// EDGE CASE AND STRESS TESTS
// ============================================================================

/**
 * @brief Test rapid server restarts (restart immediately after reconnection)
 *
 * Verifies Requirement 1.5: System should handle rapid restart cycles
 * without manual intervention.
 *
 * Test flow:
 * - Restart server immediately after reconnection completes
 * - Verify system handles rapid state changes correctly
 * - Ensure no race conditions or crashes
 */
TEST_F(ServerRestartReconnectionTest, RapidServerRestarts) {
    std::cout << "\n=== Test: Rapid Server Restarts (Edge Case) ===" << std::endl;

    ASSERT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    const int numRapidCycles = 3;
    int successfulReconnections = 0;

    for (int cycle = 1; cycle <= numRapidCycles; ++cycle) {
        std::cout << "\n--- Rapid Restart Cycle " << cycle << " ---" << std::endl;

        // Stop server
        std::cout << "Stopping server..." << std::endl;
        mockServer_->stop();

        // Wait for disconnection
        bool disconnected = waitForDisconnection(std::chrono::seconds(10));
        EXPECT_TRUE(disconnected) << "Should detect disconnection in cycle " << cycle;

        // Restart server immediately (no delay)
        std::cout << "Restarting server immediately..." << std::endl;
        ASSERT_TRUE(mockServer_->restart());

        // Wait for reconnection
        bool reconnected = waitForReconnection(std::chrono::seconds(10));

        if (reconnected) {
            successfulReconnections++;
            std::cout << "✓ Reconnected in cycle " << cycle << std::endl;

            // CRITICAL: Restart immediately after reconnection (stress test)
            // Only minimal delay to verify connection
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else {
            std::cout << "✗ Failed to reconnect in cycle " << cycle << std::endl;
        }
    }

    std::cout << "\n=== Rapid Restart Test Results ===" << std::endl;
    std::cout << "Successful reconnections: " << successfulReconnections
              << " out of " << numRapidCycles << std::endl;

    // Should handle most rapid restarts successfully
    EXPECT_GE(successfulReconnections, numRapidCycles - 1)
        << "Should handle rapid server restarts with minimal failures";

    auto stats = reconnectionManager_->getStats();
    std::cout << "\nFinal Statistics:" << std::endl;
    std::cout << "  - Total attempts: " << stats.totalReconnectionAttempts << std::endl;
    std::cout << "  - Successful: " << stats.successfulReconnections << std::endl;
    std::cout << "  - Failed: " << stats.failedReconnections << std::endl;

    std::cout << "=== Test Complete ===" << std::endl;
}

/**
 * @brief Test reconnection during active data updates
 *
 * Verifies that reconnection works correctly when server is actively
 * updating variable values. Tests that subscriptions are properly
 * restored and data updates resume after reconnection.
 *
 * Verifies Requirements: 1.4, 2.4
 */
TEST_F(ServerRestartReconnectionTest, ReconnectionDuringActiveDataUpdates) {
    std::cout << "\n=== Test: Reconnection During Active Data Updates ===" << std::endl;

    // Step 1: Add subscriptions
    std::cout << "Step 1: Adding subscriptions..." << std::endl;
    std::string nodeId = getTestNodeId(1001);
    ASSERT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));

    // Step 2: Start monitoring
    ASSERT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Step 3: Start updating variable values in background
    std::cout << "Step 2: Starting active data updates..." << std::endl;
    std::atomic<bool> updateRunning{true};
    std::atomic<int> updateCount{0};

    std::thread updateThread([this, &updateRunning, &updateCount]() {
        int value = 100;
        while (updateRunning) {
            if (mockServer_->isRunning()) {
                UA_Variant newValue = TestValueFactory::createInt32(value++);
                mockServer_->updateTestVariable(1001, newValue);
                UA_Variant_clear(&newValue);
                updateCount++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    // Let updates run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    int updatesBeforeRestart = updateCount.load();
    std::cout << "  ✓ Updates running (" << updatesBeforeRestart << " updates)" << std::endl;

    // Step 4: Restart server while updates are happening
    std::cout << "Step 3: Restarting server during active updates..." << std::endl;
    mockServer_->stop();

    bool disconnected = waitForDisconnection(std::chrono::seconds(10));
    ASSERT_TRUE(disconnected);
    std::cout << "  ✓ Disconnection detected" << std::endl;

    ASSERT_TRUE(mockServer_->restart());
    std::cout << "  ✓ Server restarted" << std::endl;

    // Step 5: Wait for reconnection
    std::cout << "Step 4: Waiting for reconnection..." << std::endl;
    bool reconnected = waitForReconnection(std::chrono::seconds(10));
    ASSERT_TRUE(reconnected);
    std::cout << "  ✓ Reconnected" << std::endl;

    // Step 6: Verify updates resume
    std::cout << "Step 5: Verifying updates resume..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    int updatesAfterReconnection = updateCount.load();

    std::cout << "  - Updates before restart: " << updatesBeforeRestart << std::endl;
    std::cout << "  - Updates after reconnection: " << updatesAfterReconnection << std::endl;

    EXPECT_GT(updatesAfterReconnection, updatesBeforeRestart)
        << "Updates should resume after reconnection";

    // Step 7: Stop update thread
    updateRunning = false;
    if (updateThread.joinable()) {
        updateThread.join();
    }

    // Verify subscription is still active
    auto activeSubscriptions = subscriptionManager_->getActiveMonitoredItems();
    EXPECT_EQ(activeSubscriptions.size(), 1)
        << "Subscription should remain active after reconnection";

    std::cout << "=== Test Complete: PASSED ===" << std::endl;
}

/**
 * @brief Test behavior when server never comes back (verify continuous retry)
 *
 * Verifies Requirement 1.5: System should continue retry attempts
 * indefinitely when server is unavailable.
 *
 * Test flow:
 * - Stop server and keep it stopped
 * - Verify system continues attempting reconnection
 * - Verify retry attempts don't stop after max retries
 * - Verify system eventually reconnects when server comes back
 */
TEST_F(ServerRestartReconnectionTest, ContinuousRetryWhenServerNeverComesBack) {
    std::cout << "\n=== Test: Continuous Retry When Server Never Comes Back ===" << std::endl;

    ASSERT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Step 1: Stop server and keep it stopped
    std::cout << "Step 1: Stopping server (will not restart immediately)..." << std::endl;
    mockServer_->stop();

    bool disconnected = waitForDisconnection(std::chrono::seconds(10));
    ASSERT_TRUE(disconnected);
    std::cout << "  ✓ Disconnection detected" << std::endl;

    // Step 2: Wait long enough to exceed max retry count
    // With config: maxRetry=5, retryDelay=500ms, maxDelay=2000ms
    // Exponential backoff: 450ms, 1012ms, 1877ms, 2000ms, 2000ms...
    // After 10 seconds, should have made several attempts
    std::cout << "Step 2: Waiting 15 seconds (exceeding max retry count)..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(15));

    // Step 3: Verify system is still trying to reconnect
    std::cout << "Step 3: Verifying continuous retry behavior..." << std::endl;
    auto stats = reconnectionManager_->getStats();

    std::cout << "  - Total attempts: " << stats.totalReconnectionAttempts << std::endl;
    std::cout << "  - Failed attempts: " << stats.failedReconnections << std::endl;
    std::cout << "  - Successful: " << stats.successfulReconnections << std::endl;

    // Should have made multiple attempts (at least 3-4 attempts in 15 seconds)
    EXPECT_GE(stats.totalReconnectionAttempts, 3)
        << "Should have made multiple reconnection attempts";

    EXPECT_EQ(stats.successfulReconnections, 0)
        << "Should have no successful reconnections yet";

    EXPECT_GT(stats.failedReconnections, 0)
        << "Should have multiple failed reconnection attempts";

    // Verify still in reconnecting state
    EXPECT_EQ(reconnectionManager_->getState(), ReconnectionManager::ReconnectionState::RECONNECTING)
        << "Should still be in RECONNECTING state";

    // Step 4: Bring server back and verify reconnection
    std::cout << "Step 4: Bringing server back online..." << std::endl;
    ASSERT_TRUE(mockServer_->restart());

    bool reconnected = waitForReconnection(std::chrono::seconds(10));
    ASSERT_TRUE(reconnected) << "Should reconnect when server comes back";
    std::cout << "  ✓ Reconnected successfully" << std::endl;

    // Verify final statistics
    auto finalStats = reconnectionManager_->getStats();
    std::cout << "\nFinal Statistics:" << std::endl;
    std::cout << "  - Total attempts: " << finalStats.totalReconnectionAttempts << std::endl;
    std::cout << "  - Successful: " << finalStats.successfulReconnections << std::endl;
    std::cout << "  - Failed: " << finalStats.failedReconnections << std::endl;

    EXPECT_GE(finalStats.successfulReconnections, 1)
        << "Should have successful reconnection after server returns";

    std::cout << "=== Test Complete: PASSED ===" << std::endl;
}

/**
 * @brief Test reconnection with aggressive retry configuration
 *
 * Verifies Requirement 2.4: System should work correctly with different
 * retry configurations.
 *
 * Test flow:
 * - Configure aggressive retry settings (fast retries, low delays)
 * - Verify reconnection works with aggressive settings
 * - Verify timing meets requirements
 */
TEST_F(ServerRestartReconnectionTest, AggressiveRetryConfiguration) {
    std::cout << "\n=== Test: Aggressive Retry Configuration ===" << std::endl;

    // Reconfigure with aggressive settings
    config_.connectionRetryMax = 10;
    config_.connectionInitialDelay = 50;    // 50ms
    config_.connectionMaxRetry = 10;
    config_.connectionMaxDelay = 500;       // 500ms max
    config_.connectionRetryDelay = 100;     // 100ms base

    // Recreate components with new config
    reconnectionManager_.reset();
    reconnectionManager_ = std::make_unique<ReconnectionManager>(
        opcClient_.get(), subscriptionManager_.get(), config_);

    std::cout << "Configuration:" << std::endl;
    std::cout << "  - Initial delay: " << config_.connectionInitialDelay << "ms" << std::endl;
    std::cout << "  - Retry delay: " << config_.connectionRetryDelay << "ms" << std::endl;
    std::cout << "  - Max delay: " << config_.connectionMaxDelay << "ms" << std::endl;
    std::cout << "  - Max retries: " << config_.connectionMaxRetry << std::endl;

    ASSERT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Stop server
    std::cout << "\nStopping server..." << std::endl;
    mockServer_->stop();

    bool disconnected = waitForDisconnection(std::chrono::seconds(10));
    ASSERT_TRUE(disconnected);
    std::cout << "  ✓ Disconnection detected" << std::endl;

    // Restart server
    std::cout << "Restarting server..." << std::endl;
    auto restartTime = std::chrono::steady_clock::now();
    ASSERT_TRUE(mockServer_->restart());

    // With aggressive settings, should reconnect very quickly
    bool reconnected = waitForReconnection(std::chrono::seconds(5));
    ASSERT_TRUE(reconnected) << "Should reconnect quickly with aggressive settings";

    auto reconnectionTime = std::chrono::steady_clock::now();
    auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
        reconnectionTime - restartTime);

    std::cout << "  ✓ Reconnected after " << delay.count() << "ms" << std::endl;

    // With aggressive settings, reconnection should be very fast
    EXPECT_LE(delay.count(), 3000)
        << "Aggressive configuration should reconnect within 3 seconds";

    auto stats = reconnectionManager_->getStats();
    std::cout << "\nStatistics:" << std::endl;
    std::cout << "  - Total attempts: " << stats.totalReconnectionAttempts << std::endl;
    std::cout << "  - Successful: " << stats.successfulReconnections << std::endl;

    std::cout << "=== Test Complete: PASSED ===" << std::endl;
}

/**
 * @brief Test reconnection with conservative retry configuration
 *
 * Verifies Requirement 2.4: System should work correctly with different
 * retry configurations.
 *
 * Test flow:
 * - Configure conservative retry settings (slow retries, high delays)
 * - Verify reconnection still works (though slower)
 * - Verify system respects configured delays
 */
TEST_F(ServerRestartReconnectionTest, ConservativeRetryConfiguration) {
    std::cout << "\n=== Test: Conservative Retry Configuration ===" << std::endl;

    // Reconfigure with conservative settings
    config_.connectionRetryMax = 3;
    config_.connectionInitialDelay = 500;   // 500ms
    config_.connectionMaxRetry = 3;
    config_.connectionMaxDelay = 5000;      // 5 seconds max
    config_.connectionRetryDelay = 1000;    // 1 second base

    // Recreate components with new config
    reconnectionManager_.reset();
    reconnectionManager_ = std::make_unique<ReconnectionManager>(
        opcClient_.get(), subscriptionManager_.get(), config_);

    std::cout << "Configuration:" << std::endl;
    std::cout << "  - Initial delay: " << config_.connectionInitialDelay << "ms" << std::endl;
    std::cout << "  - Retry delay: " << config_.connectionRetryDelay << "ms" << std::endl;
    std::cout << "  - Max delay: " << config_.connectionMaxDelay << "ms" << std::endl;
    std::cout << "  - Max retries: " << config_.connectionMaxRetry << std::endl;

    ASSERT_TRUE(reconnectionManager_->startMonitoring());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Stop server
    std::cout << "\nStopping server..." << std::endl;
    mockServer_->stop();

    bool disconnected = waitForDisconnection(std::chrono::seconds(10));
    ASSERT_TRUE(disconnected);
    std::cout << "  ✓ Disconnection detected" << std::endl;

    // Restart server
    std::cout << "Restarting server..." << std::endl;
    auto restartTime = std::chrono::steady_clock::now();
    ASSERT_TRUE(mockServer_->restart());

    // With conservative settings, reconnection will be slower
    bool reconnected = waitForReconnection(std::chrono::seconds(15));
    ASSERT_TRUE(reconnected) << "Should still reconnect with conservative settings";

    auto reconnectionTime = std::chrono::steady_clock::now();
    auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
        reconnectionTime - restartTime);

    std::cout << "  ✓ Reconnected after " << delay.count() << "ms" << std::endl;

    auto stats = reconnectionManager_->getStats();
    std::cout << "\nStatistics:" << std::endl;
    std::cout << "  - Total attempts: " << stats.totalReconnectionAttempts << std::endl;
    std::cout << "  - Successful: " << stats.successfulReconnections << std::endl;

    // Conservative settings mean fewer attempts before success
    EXPECT_LE(stats.totalReconnectionAttempts, 5)
        << "Conservative settings should result in fewer, slower attempts";

    std::cout << "=== Test Complete: PASSED ===" << std::endl;
}

} // namespace test
} // namespace opcua2http
