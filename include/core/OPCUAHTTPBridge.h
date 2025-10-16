#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include "config/Configuration.h"

namespace opcua2http {

/**
 * @brief Main application class for the OPC UA HTTP Bridge
 * 
 * This class orchestrates all components of the bridge system, including
 * OPC UA client, cache management, subscription handling, and HTTP server.
 */
class OPCUAHTTPBridge {
public:
    OPCUAHTTPBridge();
    ~OPCUAHTTPBridge();
    
    // Non-copyable and non-movable
    OPCUAHTTPBridge(const OPCUAHTTPBridge&) = delete;
    OPCUAHTTPBridge& operator=(const OPCUAHTTPBridge&) = delete;
    OPCUAHTTPBridge(OPCUAHTTPBridge&&) = delete;
    OPCUAHTTPBridge& operator=(OPCUAHTTPBridge&&) = delete;
    
    /**
     * @brief Initialize the application and all components
     * @return true if initialization successful, false otherwise
     */
    bool initialize();
    
    /**
     * @brief Start the HTTP server and begin processing requests
     * This method blocks until the server is stopped
     */
    void run();
    
    /**
     * @brief Stop the server and shutdown all components gracefully
     */
    void stop();
    
    /**
     * @brief Check if the application is currently running
     * @return true if running, false otherwise
     */
    bool isRunning() const { return running_.load(); }
    
    /**
     * @brief Get the current configuration
     * @return Reference to the configuration object
     */
    const Configuration& getConfiguration() const { return *config_; }
    
private:
    // Core components
    std::unique_ptr<Configuration> config_;
    
    // Crow HTTP application (using void* to avoid forward declaration issues)
    void* app_;
    
    // Runtime state
    std::atomic<bool> running_;
    std::thread serverThread_;
    
    // Initialization methods
    bool initializeConfiguration();
    bool initializeOPCClient();
    bool initializeComponents();
    bool setupHTTPServer();
    
    // Signal handling
    static void signalHandler(int signal);
    static OPCUAHTTPBridge* instance_;
    void setupSignalHandlers();
    
    // Cleanup
    void cleanup();
};

} // namespace opcua2http