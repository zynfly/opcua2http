#include <iostream>
#include "core/OPCUAHTTPBridge.h"

int main() {
    try {
        // Create the bridge application
        opcua2http::OPCUAHTTPBridge bridge;
        
        // Initialize all components
        if (!bridge.initialize()) {
            std::cerr << "Failed to initialize OPC UA HTTP Bridge" << std::endl;
            return 1;
        }
        
        std::cout << "OPC UA HTTP Bridge starting..." << std::endl;
        
        // Run the server (this blocks until shutdown)
        bridge.run();
        
        std::cout << "OPC UA HTTP Bridge shutdown complete" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return 1;
    }
    
    return 0;
}