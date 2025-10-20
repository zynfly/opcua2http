#pragma once

#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include "config/Configuration.h"
#include "crow.h"
#include <crow/middlewares/cors.h>

namespace opcua2http {

// Forward declarations
class OPCUAClient;
class CacheManager;
class CacheMetrics;
class APIHandler;
class ReadStrategy;
class BackgroundUpdater;
class CacheErrorHandler;

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
     * @brief Start the HTTP server in a separate thread (non-blocking)
     * @return true if started successfully, false otherwise
     */
    bool startAsync();

    /**
     * @brief Stop the server and shutdown all components gracefully
     */
    void stop();

    /**
     * @brief Get runtime statistics for monitoring
     * @return JSON string with current system status
     */
    std::string getStatus() const;

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
    // Configuration
    std::unique_ptr<Configuration> config_;

    // Core components
    std::unique_ptr<OPCUAClient> opcClient_;
    std::unique_ptr<CacheManager> cacheManager_;
    std::unique_ptr<CacheMetrics> cacheMetrics_;
    std::unique_ptr<CacheErrorHandler> errorHandler_;
    std::unique_ptr<ReadStrategy> readStrategy_;
    std::unique_ptr<BackgroundUpdater> backgroundUpdater_;
    std::unique_ptr<APIHandler> apiHandler_;

    // Crow HTTP application with CORS middleware
    crow::App<crow::CORSHandler> app_;

    // Runtime state
    std::atomic<bool> running_;
    std::thread serverThread_;
    std::thread cleanupThread_;
    std::chrono::steady_clock::time_point startTime_;

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
