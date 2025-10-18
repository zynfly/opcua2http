#include "core/OPCUAHTTPBridge.h"
#include "core/ErrorHandler.h"
#include <spdlog/spdlog.h>
#include "opcua/OPCUAClient.h"
#include "cache/CacheManager.h"
#include "subscription/SubscriptionManager.h"
#include "reconnection/ReconnectionManager.h"
#include "http/APIHandler.h"
#include <iostream>
#include <csignal>
#include <chrono>
#include <crow.h>

namespace opcua2http {

// Static instance for signal handling
OPCUAHTTPBridge* OPCUAHTTPBridge::instance_ = nullptr;

OPCUAHTTPBridge::OPCUAHTTPBridge() 
    : running_(false) {
    instance_ = this;
}

OPCUAHTTPBridge::~OPCUAHTTPBridge() {
    if (running_.load()) {
        stop();
    }
    cleanup();
    instance_ = nullptr;
}

bool OPCUAHTTPBridge::initialize() {
    return ErrorHandler::executeWithErrorHandling([this]() {
        spdlog::info("Initializing OPC UA HTTP Bridge...");
        
        // Initialize configuration
        if (!initializeConfiguration()) {
            throw std::runtime_error("Failed to initialize configuration");
        }
        
        // Set log level from configuration
        spdlog::set_level(spdlog::level::info); // Default, can be configured later
        
        spdlog::info("Configuration loaded successfully");
        spdlog::debug("Configuration details: {}", config_->toString());
        
        // Setup signal handlers for graceful shutdown
        setupSignalHandlers();
        spdlog::debug("Signal handlers configured");
        
        // Initialize OPC UA client
        if (!initializeOPCClient()) {
            throw std::runtime_error("Failed to initialize OPC UA client");
        }
        
        // Initialize other components
        if (!initializeComponents()) {
            throw std::runtime_error("Failed to initialize components");
        }
        
        // Setup HTTP server
        if (!setupHTTPServer()) {
            throw std::runtime_error("Failed to setup HTTP server");
        }
        
        spdlog::info("OPC UA HTTP Bridge initialized successfully");
        
    }, "OPCUAHTTPBridge::initialize");
}

void OPCUAHTTPBridge::run() {
    ErrorHandler::executeWithErrorHandling([this]() {
        if (!app_) {
            throw std::runtime_error("HTTP server not initialized");
        }
        
        running_.store(true);
        startTime_ = std::chrono::steady_clock::now();
        
        spdlog::info("Starting OPC UA HTTP Bridge...");
        spdlog::info("Configuration:");
        spdlog::info("  OPC UA Endpoint: {}", config_->opcEndpoint);
        spdlog::info("  HTTP Port: {}", config_->serverPort);
        spdlog::info("  Cache Expire: {} minutes", config_->cacheExpireMinutes);
        spdlog::info("  Subscription Cleanup: {} minutes", config_->subscriptionCleanupMinutes);
        spdlog::info("  Log Level: {}", config_->logLevel);
        
        // Start reconnection monitoring
        reconnectionManager_->startMonitoring();
        spdlog::info("✓ Reconnection monitoring started");
        
        // Start cache cleanup thread with enhanced logging
        cleanupThread_ = std::thread([this]() {
            spdlog::debug("Cache cleanup thread started");
            
            while (running_.load()) {
                std::this_thread::sleep_for(std::chrono::minutes(5)); // Cleanup every 5 minutes
                
                if (running_.load()) {
                    ErrorHandler::executeWithErrorHandling([this]() {
                        auto beforeCache = cacheManager_->getCachedNodeIds().size();
                        auto beforeSubs = subscriptionManager_->getActiveMonitoredItems().size();
                        
                        cacheManager_->cleanupExpiredEntries();
                        subscriptionManager_->cleanupUnusedItems();
                        
                        auto afterCache = cacheManager_->getCachedNodeIds().size();
                        auto afterSubs = subscriptionManager_->getActiveMonitoredItems().size();
                        
                        if (beforeCache != afterCache || beforeSubs != afterSubs) {
                            spdlog::info("Cleanup completed - Cache: {}→{}, Subscriptions: {}→{}", 
                                       beforeCache, afterCache, beforeSubs, afterSubs);
                        }
                        
                    }, "Cache cleanup");
                }
            }
            
            spdlog::debug("Cache cleanup thread stopped");
        });
        
        // Log startup completion
        spdlog::info("✓ All background services started");
        spdlog::info("✓ HTTP server starting on port {}", config_->serverPort);
        spdlog::info("✓ OPC UA HTTP Bridge is ready to serve requests");
        spdlog::info("✓ Health check available at: http://localhost:{}/health", config_->serverPort);
        spdlog::info("✓ API endpoint available at: http://localhost:{}/iotgateway/read", config_->serverPort);
        
        // Start HTTP server (this blocks)
        app_->port(static_cast<uint16_t>(config_->serverPort))
            .multithreaded()
            .run();
            
    }, "Server runtime", [this]() -> bool {
        // Recovery: attempt graceful shutdown
        spdlog::warn("Attempting graceful shutdown due to server error...");
        stop();
        return false; // Don't continue after error
    });
    
    running_.store(false);
    spdlog::info("HTTP server stopped");
}

void OPCUAHTTPBridge::stop() {
    if (!running_.load()) {
        return;
    }
    
    spdlog::info("Stopping OPC UA HTTP Bridge...");
    
    running_.store(false);
    
    ErrorHandler::executeWithErrorHandling([this]() {
        // Stop HTTP server
        if (app_) {
            app_->stop();
            spdlog::debug("HTTP server stopped");
        }
        
        // Stop reconnection monitoring
        if (reconnectionManager_) {
            reconnectionManager_->stopMonitoring();
            spdlog::debug("Reconnection monitoring stopped");
        }
        
        // Wait for cleanup thread
        if (cleanupThread_.joinable()) {
            cleanupThread_.join();
            spdlog::debug("Cleanup thread joined");
        }
        
        // Wait for server thread if it's running
        if (serverThread_.joinable()) {
            serverThread_.join();
            spdlog::debug("Server thread joined");
        }
        
    }, "Graceful shutdown");
    
    spdlog::info("OPC UA HTTP Bridge stopped");
}

bool OPCUAHTTPBridge::initializeConfiguration() {
    return ErrorHandler::executeWithErrorHandling([this]() {
        config_ = std::make_unique<Configuration>(Configuration::loadFromEnvironment());
        
        if (!config_->validate()) {
            throw std::runtime_error("Configuration validation failed");
        }
        
    }, "Configuration initialization");
}

bool OPCUAHTTPBridge::initializeOPCClient() {
    return ErrorHandler::executeWithErrorHandling([this]() {
        spdlog::info("Initializing OPC UA client...");
        
        // Create OPC UA client
        opcClient_ = std::make_unique<OPCUAClient>();
        
        // Initialize with configuration
        if (!opcClient_->initialize(*config_)) {
            throw std::runtime_error("Failed to initialize OPC UA client with configuration");
        }
        
        // Attempt initial connection
        if (!opcClient_->connect()) {
            throw std::runtime_error("Failed to connect to OPC UA server: " + config_->opcEndpoint);
        }
        
        spdlog::info("OPC UA client connected successfully to: {}", config_->opcEndpoint);
        
    }, "OPC UA client initialization", [this]() -> bool {
        // Recovery: try to reconnect
        spdlog::warn("Attempting OPC UA client recovery...");
        return opcClient_ && opcClient_->connect();
    });
}

bool OPCUAHTTPBridge::initializeComponents() {
    return ErrorHandler::executeWithErrorHandling([this]() {
        spdlog::info("Initializing core components...");
        
        // Initialize Cache Manager
        cacheManager_ = std::make_unique<CacheManager>(
            std::chrono::minutes(config_->cacheExpireMinutes)
        );
        spdlog::debug("Cache manager initialized");
        
        // Initialize Subscription Manager
        subscriptionManager_ = std::make_unique<SubscriptionManager>(
            opcClient_->getClient(), 
            cacheManager_.get(),
            std::chrono::minutes(config_->subscriptionCleanupMinutes)
        );
        
        if (!subscriptionManager_->initializeSubscription()) {
            throw std::runtime_error("Failed to initialize subscription manager");
        }
        spdlog::debug("Subscription manager initialized");
        
        // Initialize Reconnection Manager
        reconnectionManager_ = std::make_unique<ReconnectionManager>(
            opcClient_.get(), 
            subscriptionManager_.get(),
            *config_
        );
        spdlog::debug("Reconnection manager initialized");
        
        // Initialize API Handler
        apiHandler_ = std::make_unique<APIHandler>(
            cacheManager_.get(),
            subscriptionManager_.get(),
            opcClient_.get(),
            *config_
        );
        spdlog::debug("API handler initialized");
        
        spdlog::info("All core components initialized successfully");
        
    }, "Components initialization");
}

bool OPCUAHTTPBridge::setupHTTPServer() {
    return ErrorHandler::executeWithErrorHandling([this]() {
        spdlog::info("Setting up HTTP server...");
        
        // Create Crow application
        app_ = std::make_unique<crow::SimpleApp>();
        
        // Setup routes through API handler
        apiHandler_->setupRoutes(*app_);
        
        spdlog::info("HTTP server routes configured");
        
    }, "HTTP server setup");
}

void OPCUAHTTPBridge::signalHandler(int signal) {
    if (instance_) {
        spdlog::info("Received signal {}, shutting down...", signal);
        instance_->stop();
    }
}

void OPCUAHTTPBridge::setupSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
}

void OPCUAHTTPBridge::cleanup() {
    spdlog::info("Cleaning up resources...");
    
    ErrorHandler::executeWithErrorHandling([this]() {
        // Stop all components gracefully
        if (reconnectionManager_) {
            reconnectionManager_->stopMonitoring();
            spdlog::debug("Reconnection manager stopped");
        }
        
        // Disconnect OPC UA client
        if (opcClient_) {
            opcClient_->disconnect();
            spdlog::debug("OPC UA client disconnected");
        }
        
        // Clear all components in reverse order of initialization
        apiHandler_.reset();
        spdlog::debug("API handler cleaned up");
        
        reconnectionManager_.reset();
        spdlog::debug("Reconnection manager cleaned up");
        
        subscriptionManager_.reset();
        spdlog::debug("Subscription manager cleaned up");
        
        cacheManager_.reset();
        spdlog::debug("Cache manager cleaned up");
        
        opcClient_.reset();
        spdlog::debug("OPC UA client cleaned up");
        
        app_.reset();
        spdlog::debug("HTTP server cleaned up");
        
        config_.reset();
        spdlog::debug("Configuration cleaned up");
        
    }, "Resource cleanup");
    
    spdlog::info("Resources cleaned up");
}

std::string OPCUAHTTPBridge::getStatus() const {
    try {
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
        
        // Build status JSON
        std::string status = R"({
  "service": "opcua-http-bridge",
  "status": ")" + (running_.load() ? std::string("running") : std::string("stopped")) + R"(",
  "uptime_seconds": )" + std::to_string(uptime) + R"(,
  "opc_connected": )" + (opcClient_ && opcClient_->isConnected() ? "true" : "false") + R"(,
  "cached_items": )" + std::to_string(cacheManager_ ? cacheManager_->getCachedNodeIds().size() : 0) + R"(,
  "active_subscriptions": )" + std::to_string(subscriptionManager_ ? subscriptionManager_->getActiveMonitoredItems().size() : 0) + R"(,
  "configuration": {
    "opc_endpoint": ")" + (config_ ? config_->opcEndpoint : "unknown") + R"(",
    "server_port": )" + std::to_string(config_ ? config_->serverPort : 0) + R"(,
    "cache_expire_minutes": )" + std::to_string(config_ ? config_->cacheExpireMinutes : 0) + R"(
  }
})";
        
        return status;
        
    } catch (const std::exception& e) {
        return R"({"error": "Failed to get status: )" + std::string(e.what()) + R"("})";
    }
}

} // namespace opcua2http