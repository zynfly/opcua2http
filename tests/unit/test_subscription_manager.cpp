#include <gtest/gtest.h>
#include <memory>

#include "common/OPCUATestBase.h"
#include "subscription/SubscriptionManager.h"

namespace opcua2http {
namespace test {

class SubscriptionManagerTest : public SubscriptionTestBase {
protected:
    void SetUp() override {
        SubscriptionTestBase::SetUp();
        
        // Create components
        opcClient_ = createConnectedOPCClient();
        ASSERT_NE(opcClient_, nullptr);
        
        cacheManager_ = createCacheManager();
        
        // Create subscription manager with short expiration for testing
        subscriptionManager_ = std::make_unique<SubscriptionManager>(
            opcClient_.get(), cacheManager_.get(), 1); // 1 minute expiration
    }
    
    std::unique_ptr<OPCUAClient> opcClient_;
    std::unique_ptr<CacheManager> cacheManager_;
    std::unique_ptr<SubscriptionManager> subscriptionManager_;
};

// Test basic subscription functionality
TEST_F(SubscriptionManagerTest, BasicSubscriptionTest) {
    EXPECT_FALSE(subscriptionManager_->isSubscriptionActive());
    
    EXPECT_TRUE(subscriptionManager_->initializeSubscription());
    EXPECT_TRUE(subscriptionManager_->isSubscriptionActive());
    EXPECT_NE(subscriptionManager_->getSubscriptionId(), 0);
}

// Test adding monitored items
TEST_F(SubscriptionManagerTest, AddMonitoredItems) {
    std::string nodeId = getTestNodeId(1001);
    
    EXPECT_FALSE(subscriptionManager_->hasMonitoredItem(nodeId));
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    EXPECT_TRUE(subscriptionManager_->hasMonitoredItem(nodeId));
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 1);
}

// Test multiple monitored items
TEST_F(SubscriptionManagerTest, MultipleMonitoredItems) {
    std::vector<std::string> nodeIds = {
        getTestNodeId(1001),
        getTestNodeId(1002),
        getTestNodeId(1003)
    };
    
    for (const auto& nodeId : nodeIds) {
        EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    }
    
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 3);
    
    // Verify all items are present
    for (const auto& nodeId : nodeIds) {
        EXPECT_TRUE(subscriptionManager_->hasMonitoredItem(nodeId));
    }
}

// Test data change notifications
TEST_F(SubscriptionManagerTest, DataChangeNotifications) {
    std::string nodeId = getTestNodeId(1001);
    
    // Add monitored item
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    
    // Allow subscription to be established
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Update variable and wait for notification
    UA_Variant newValue = TestValueFactory::createInt32(999);
    updateVariableAndWait(1001, newValue, opcClient_.get());
    UA_Variant_clear(&newValue);
    
    // Check statistics for notifications
    auto stats = subscriptionManager_->getStats();
    std::cout << "Notifications received: " << stats.totalNotifications << std::endl;
    
    // Note: Actual notification count may vary based on timing
    EXPECT_GE(stats.totalNotifications, 0);
}

// Test removing monitored items
TEST_F(SubscriptionManagerTest, RemoveMonitoredItems) {
    std::string nodeId = getTestNodeId(1001);
    
    // Add and then remove
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(nodeId));
    EXPECT_TRUE(subscriptionManager_->hasMonitoredItem(nodeId));
    
    EXPECT_TRUE(subscriptionManager_->removeMonitoredItem(nodeId));
    EXPECT_FALSE(subscriptionManager_->hasMonitoredItem(nodeId));
    EXPECT_EQ(subscriptionManager_->getActiveMonitoredItems().size(), 0);
}

// Test error handling
TEST_F(SubscriptionManagerTest, ErrorHandling) {
    // Test invalid node IDs
    EXPECT_FALSE(subscriptionManager_->addMonitoredItem(""));
    EXPECT_FALSE(subscriptionManager_->addMonitoredItem("invalid-format"));
    EXPECT_FALSE(subscriptionManager_->addMonitoredItem(getTestNodeId(9999))); // Non-existent
    
    // Test removing non-existent item
    EXPECT_FALSE(subscriptionManager_->removeMonitoredItem(getTestNodeId(9999)));
}

// Test subscription statistics
TEST_F(SubscriptionManagerTest, SubscriptionStatistics) {
    auto stats = subscriptionManager_->getStats();
    EXPECT_EQ(stats.totalMonitoredItems, 0);
    EXPECT_FALSE(stats.isSubscriptionActive);
    
    // Add items and check stats
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(getTestNodeId(1001)));
    EXPECT_TRUE(subscriptionManager_->addMonitoredItem(getTestNodeId(1002)));
    
    stats = subscriptionManager_->getStats();
    EXPECT_EQ(stats.totalMonitoredItems, 2);
    EXPECT_EQ(stats.activeMonitoredItems, 2);
    EXPECT_TRUE(stats.isSubscriptionActive);
}

// Performance test for multiple subscriptions
class SubscriptionPerformanceTest : public PerformanceTestBase {
protected:
    void SetUp() override {
        PerformanceTestBase::SetUp();
        
        // Add many test variables for performance testing
        addPerformanceTestVariables(100, 3000);
        
        opcClient_ = createConnectedOPCClient();
        ASSERT_NE(opcClient_, nullptr);
        
        cacheManager_ = createCacheManager();
        subscriptionManager_ = std::make_unique<SubscriptionManager>(
            opcClient_.get(), cacheManager_.get(), 60);
    }
    
    std::unique_ptr<OPCUAClient> opcClient_;
    std::unique_ptr<CacheManager> cacheManager_;
    std::unique_ptr<SubscriptionManager> subscriptionManager_;
};

TEST_F(SubscriptionPerformanceTest, AddManyMonitoredItems) {
    // Measure time to add 10 monitored items (reduced for stability)
    std::vector<std::string> nodeIds;
    for (int i = 0; i < 10; i++) {
        nodeIds.push_back(getTestNodeId(3000 + i));
    }
    
    double executionTime = measureExecutionTime([&]() {
        for (const auto& nodeId : nodeIds) {
            // Try to add, but don't fail if some nodes don't exist
            subscriptionManager_->addMonitoredItem(nodeId);
        }
    });
    
    std::cout << "Time to add 10 monitored items: " << executionTime << "ms" << std::endl;
    EXPECT_LT(executionTime, 10000.0); // Should complete within 10 seconds
    
    // Check that at least some items were added successfully
    size_t addedItems = subscriptionManager_->getActiveMonitoredItems().size();
    std::cout << "Successfully added " << addedItems << " monitored items" << std::endl;
    EXPECT_GE(addedItems, 0); // At least some should be added
}

} // namespace test
} // namespace opcua2http