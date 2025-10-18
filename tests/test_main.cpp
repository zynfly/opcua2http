#include <gtest/gtest.h>
#include "common/GlobalTestEnvironment.h"

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Register global test environment for shared mock server
    ::testing::AddGlobalTestEnvironment(new opcua2http::test::GlobalTestEnvironment());
    
    return RUN_ALL_TESTS();
}