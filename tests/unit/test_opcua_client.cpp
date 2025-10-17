#include <gtest/gtest.h>
#include <memory>

#include "common/OPCUATestBase.h"
#include "opcua/OPCUAClient.h"

namespace opcua2http {
namespace test {

class OPCUAClientTest : public OPCUATestBase {
protected:
    // No need for SetUp/TearDown - handled by base class
    // Standard test variables (1001, 1002, 1003) are automatically available
};

// Test client initialization and connection
TEST_F(OPCUAClientTest, BasicConnectionTest) {
    auto client = createConnectedOPCClient();
    ASSERT_NE(client, nullptr);
    EXPECT_TRUE(client->isConnected());
    EXPECT_EQ(client->getConnectionState(), OPCUAClient::ConnectionState::CONNECTED);
}

// Test reading standard test variables
TEST_F(OPCUAClientTest, ReadStandardVariables) {
    auto client = createConnectedOPCClient();
    ASSERT_NE(client, nullptr);
    
    // Read integer variable
    ReadResult intResult = client->readNode(getTestNodeId(1001));
    EXPECT_TRUE(intResult.success);
    EXPECT_EQ(intResult.value, "42");
    
    // Read string variable
    ReadResult stringResult = client->readNode(getTestNodeId(1002));
    EXPECT_TRUE(stringResult.success);
    EXPECT_EQ(stringResult.value, "Hello World");
    
    // Read boolean variable
    ReadResult boolResult = client->readNode(getTestNodeId(1003));
    EXPECT_TRUE(boolResult.success);
    EXPECT_EQ(boolResult.value, "true");
}

// Test reading multiple nodes
TEST_F(OPCUAClientTest, ReadMultipleNodes) {
    auto client = createConnectedOPCClient();
    ASSERT_NE(client, nullptr);
    
    std::vector<std::string> nodeIds = {
        getTestNodeId(1001),
        getTestNodeId(1002),
        getTestNodeId(1003)
    };
    
    std::vector<ReadResult> results = client->readNodes(nodeIds);
    ASSERT_EQ(results.size(), 3);
    
    EXPECT_TRUE(results[0].success);
    EXPECT_EQ(results[0].value, "42");
    
    EXPECT_TRUE(results[1].success);
    EXPECT_EQ(results[1].value, "Hello World");
    
    EXPECT_TRUE(results[2].success);
    EXPECT_EQ(results[2].value, "true");
}

// Test error handling
TEST_F(OPCUAClientTest, ErrorHandling) {
    auto client = createConnectedOPCClient();
    ASSERT_NE(client, nullptr);
    
    // Test non-existent node
    ReadResult result = client->readNode(getTestNodeId(9999));
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.value.empty());
    
    // Test invalid node ID format
    result = client->readNode("invalid-node-id");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.reason, "Invalid NodeId format");
}

// Custom test with additional variables
class CustomVariableTest : public OPCUATestBase {
protected:
    CustomVariableTest() : OPCUATestBase(0, false) {} // Don't use standard variables
    
    void SetUp() override {
        OPCUATestBase::SetUp();
        
        // Add custom test variables
        UA_Variant doubleValue = TestValueFactory::createDouble(3.14159);
        mockServer_->addTestVariable(2001, "CustomDouble", doubleValue);
        UA_Variant_clear(&doubleValue);
        
        UA_Variant floatValue = TestValueFactory::createFloat(2.718f);
        mockServer_->addTestVariable(2002, "CustomFloat", floatValue);
        UA_Variant_clear(&floatValue);
    }
};

TEST_F(CustomVariableTest, ReadCustomVariables) {
    auto client = createConnectedOPCClient();
    ASSERT_NE(client, nullptr);
    
    // Read double variable
    ReadResult doubleResult = client->readNode(getTestNodeId(2001));
    EXPECT_TRUE(doubleResult.success);
    // Use approximate comparison for floating point values
    EXPECT_TRUE(doubleResult.value.find("3.14") != std::string::npos);
    
    // Read float variable
    ReadResult floatResult = client->readNode(getTestNodeId(2002));
    EXPECT_TRUE(floatResult.success);
    EXPECT_TRUE(floatResult.value.find("2.7") != std::string::npos);
}

} // namespace test
} // namespace opcua2http