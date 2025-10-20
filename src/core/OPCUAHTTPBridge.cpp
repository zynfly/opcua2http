#include "core/OPCUAHTTPBridge.h"
#include "core/ErrorHandler.h"
#include <spdlog/spdlog.h>
#include "opcua/OPCUAClient.h"
#include "cache/CacheManager.h"
#include "cache/CacheMetrics.h"
#include "core/ReadStrategy.h"
#include "core/BackgroundUpdater.h"
#include "core/CacheErrorHandler.h"
#include "http/APIHandler.h"
#include <iostream>
#include <csignal>
#include <chrono>
#include <future>
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

    // Wait for server thread to finish
    if (serverThread_.joinable()) {
        spdlog::debug("Destructor waiting for server thread...");

        auto future = std::async(std::launch::async, [this]() {
            serverThread_.join();
        });

        if (future.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) {
            spdlog::warn("Destructor: Server thread did not join within timeout, detaching");
            serverThread_.detach();
        } else {
            spdlog::debug("Destructor: Server thread joined successfully");
        }
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
        // HTTP server is now a direct member, always initialized

        running_.store(true);
        startTime_ = std::chrono::steady_clock::now();

        spdlog::info("Starting OPC UA HTTP Bridge...");
        spdlog::info("Configuration:");
        spdlog::info("  OPC UA Endpoint: {}", config_->opcEndpoint);
        spdlog::info("  HTTP Port: {}", config_->serverPort);
        spdlog::info("  Cache Refresh Threshold: {}s", config_->cacheRefreshThresholdSeconds);
        spdlog::info("  Cache Expire: {}s", config_->cacheExpireSeconds);
        spdlog::info("  Cache Cleanup Interval: {}s", config_->cacheCleanupIntervalSeconds);
        spdlog::info("  Background Update Threads: {}", config_->backgroundUpdateThreads);
        spdlog::info("  Log Level: {}", config_->logLevel);

        // Start background updater
        backgroundUpdater_->start();
        spdlog::info("✓ Background updater started with {} worker threads", config_->backgroundUpdateThreads);

        // Start cache cleanup thread with enhanced logging
        cleanupThread_ = std::thread([this]() {
            spdlog::debug("Cache cleanup thread started");

            while (running_.load()) {
                // Sleep in small intervals to allow quick shutdown
                auto cleanupInterval = std::chrono::seconds(config_->cacheCleanupIntervalSeconds);
                auto checkInterval = std::chrono::milliseconds(500);
                auto elapsed = std::chrono::milliseconds(0);

                while (running_.load() && elapsed < cleanupInterval) {
                    std::this_thread::sleep_for(checkInterval);
                    elapsed += checkInterval;
                }

                if (running_.load()) {
                    ErrorHandler::executeWithErrorHandling([this]() {
                        auto beforeCache = cacheManager_->getCachedNodeIds().size();

                        cacheManager_->cleanupExpiredEntries();

                        auto afterCache = cacheManager_->getCachedNodeIds().size();

                        if (beforeCache != afterCache) {
                            spdlog::info("Cache cleanup completed - Entries: {}→{}",
                                       beforeCache, afterCache);
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
        app_.port(static_cast<uint16_t>(config_->serverPort))
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

bool OPCUAHTTPBridge::startAsync() {
    if (running_.load()) {
        spdlog::warn("Bridge is already running");
        return false;
    }

    try {
        // Start the server in a separate thread
        serverThread_ = std::thread([this]() {
            try {
                run();
            } catch (const std::exception& e) {
                spdlog::error("Server thread error: {}", e.what());
            }
            spdlog::debug("Server thread exiting");
        });

        // Wait a bit for the server to start
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        return running_.load();
    } catch (const std::exception& e) {
        spdlog::error("Failed to start bridge async: {}", e.what());
        return false;
    }
}

void OPCUAHTTPBridge::stop() {
    // Use atomic flag to prevent multiple stops
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        spdlog::debug("Stop already called or not running");
        return;
    }

    spdlog::info("Stopping OPC UA HTTP Bridge...");

    ErrorHandler::executeWithErrorHandling([this]() {
        // Stop HTTP server first
        app_.stop();
        spdlog::debug("HTTP server stop signal sent");

        // Stop background updater
        if (backgroundUpdater_) {
            backgroundUpdater_->stop();
            spdlog::debug("Background updater stopped");
        }

        // Wait for cleanup thread
        if (cleanupThread_.joinable()) {
            cleanupThread_.join();
            spdlog::debug("Cleanup thread joined");
        }

        // Wait for server thread if it's running (with timeout)
        if (serverThread_.joinable()) {
            spdlog::debug("Waiting for server thread to join...");

            // Try to join with timeout
            auto future = std::async(std::launch::async, [this]() {
                serverThread_.join();
            });

            if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                spdlog::warn("Server thread did not join within timeout, detaching");
                serverThread_.detach();
            } else {
                spdlog::debug("Server thread joined successfully");
            }
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

        // Initialize Cache Manager with new cache timing configuration
        cacheManager_ = std::make_unique<CacheManager>(
            config_->cacheExpireMinutes,
            config_->cacheMaxEntries,
            config_->cacheRefreshThresholdSeconds,
            config_->cacheExpireSeconds
        );
        spdlog::debug("Cache manager initialized with refresh threshold: {}s, expire: {}s, max entries: {}",
                     config_->cacheRefreshThresholdSeconds,
                     config_->cacheExpireSeconds,
                     config_->cacheMaxEntries);

        // Initialize BackgroundUpdater
        backgroundUpdater_ = std::make_unique<BackgroundUpdater>(
            cacheManager_.get(),
            opcClient_.get()
        );

        // Configure background updater from configuration
        backgroundUpdater_->setMaxConcurrentUpdates(config_->backgroundUpdateThreads);
        backgroundUpdater_->setUpdateQueueSize(config_->backgroundUpdateQueueSize);
        backgroundUpdater_->setUpdateTimeout(std::chrono::milliseconds(config_->backgroundUpdateTimeoutMs));

        spdlog::debug("Background updater initialized with {} threads, queue size: {}, timeout: {}ms",
                     config_->backgroundUpdateThreads,
                     config_->backgroundUpdateQueueSize,
                     config_->backgroundUpdateTimeoutMs);

        // Initialize CacheMetrics
        cacheMetrics_ = std::make_unique<CacheMetrics>(
            cacheManager_.get(),
            backgroundUpdater_.get()
        );
        spdlog::debug("Cache metrics initialized");

        // Initialize CacheErrorHandler
        errorHandler_ = std::make_unique<CacheErrorHandler>(
            cacheManager_.get(),
            opcClient_.get()
        );
        spdlog::debug("Cache error handler initialized");

        // Initialize ReadStrategy
        readStrategy_ = std::make_unique<ReadStrategy>(
            cacheManager_.get(),
            opcClient_.get(),
            errorHandler_.get()
        );

        // Set background updater for ReadStrategy
        readStrategy_->setBackgroundUpdater(backgroundUpdater_.get());

        // Configure ReadStrategy from configuration
        readStrategy_->setMaxConcurrentReads(config_->cacheConcurrentReads);

        spdlog::debug("Read strategy initialized with max concurrent reads: {}",
                     config_->cacheConcurrentReads);

        // Initialize API Handler
        apiHandler_ = std::make_unique<APIHandler>(
            cacheManager_.get(),
            readStrategy_.get(),
            opcClient_.get(),
            *config_,
            cacheMetrics_.get(),
            errorHandler_.get()
        );
        spdlog::debug("API handler initialized");

        spdlog::info("All core components initialized successfully");

    }, "Components initialization");
}

bool OPCUAHTTPBridge::setupHTTPServer() {
    return ErrorHandler::executeWithErrorHandling([this]() {
        spdlog::info("Setting up HTTP server...");

        // Setup routes through API handler
        apiHandler_->setupRoutes(app_);

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
        // Stop background updater
        if (backgroundUpdater_) {
            backgroundUpdater_->stop();
            spdlog::debug("Background updater stopped");
        }

        // Disconnect OPC UA client
        if (opcClient_) {
            opcClient_->disconnect();
            spdlog::debug("OPC UA client disconnected");
        }

        // Clear all components in reverse order of initialization
        apiHandler_.reset();
        spdlog::debug("API handler cleaned up");

        readStrategy_.reset();
        spdlog::debug("Read strategy cleaned up");

        errorHandler_.reset();
        spdlog::debug("Error handler cleaned up");

        cacheMetrics_.reset();
        spdlog::debug("Cache metrics cleaned up");

        backgroundUpdater_.reset();
        spdlog::debug("Background updater cleaned up");

        cacheManager_.reset();
        spdlog::debug("Cache manager cleaned up");

        opcClient_.reset();
        spdlog::debug("OPC UA client cleaned up");

        // HTTP server is now a direct member, no need to reset
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

        // Get cache statistics
        auto cacheStats = cacheManager_ ? cacheManager_->getStats() : CacheManager::CacheStats{};

        // Get background updater statistics
        auto bgStats = backgroundUpdater_ ? backgroundUpdater_->getStats() : BackgroundUpdater::UpdateStats{};

        // Get enhanced cache metrics if available
        std::string cacheMetricsJson = "null";
        if (cacheMetrics_) {
            auto metricsJson = cacheMetrics_->getMetricsJSON(true);
            cacheMetricsJson = metricsJson.dump();
        }

        // Build status JSON
        std::string status = R"({
  "service": "opcua-http-bridge",
  "status": ")" + (running_.load() ? std::string("running") : std::string("stopped")) + R"(",
  "uptime_seconds": )" + std::to_string(uptime) + R"(,
  "opc_connected": )" + (opcClient_ && opcClient_->isConnected() ? "true" : "false") + R"(,
  "cache": {
    "total_entries": )" + std::to_string(cacheStats.totalEntries) + R"(,
    "subscribed_entries": )" + std::to_string(cacheStats.subscribedEntries) + R"(,
    "expired_entries": )" + std::to_string(cacheStats.expiredEntries) + R"(,
    "total_hits": )" + std::to_string(cacheStats.totalHits) + R"(,
    "total_misses": )" + std::to_string(cacheStats.totalMisses) + R"(,
    "hit_ratio": )" + std::to_string(cacheStats.hitRatio) + R"(
  },
  "cache_metrics": )" + cacheMetricsJson + R"(,
  "background_updates": {
    "total_updates": )" + std::to_string(bgStats.totalUpdates) + R"(,
    "successful_updates": )" + std::to_string(bgStats.successfulUpdates) + R"(,
    "failed_updates": )" + std::to_string(bgStats.failedUpdates) + R"(,
    "queued_updates": )" + std::to_string(bgStats.queuedUpdates) + R"(,
    "average_update_time_ms": )" + std::to_string(bgStats.averageUpdateTime) + R"(
  },
  "configuration": {
    "opc_endpoint": ")" + (config_ ? config_->opcEndpoint : "unknown") + R"(",
    "server_port": )" + std::to_string(config_ ? config_->serverPort : 0) + R"(,
    "cache_refresh_threshold_seconds": )" + std::to_string(config_ ? config_->cacheRefreshThresholdSeconds : 0) + R"(,
    "cache_expire_seconds": )" + std::to_string(config_ ? config_->cacheExpireSeconds : 0) + R"(,
    "background_update_threads": )" + std::to_string(config_ ? config_->backgroundUpdateThreads : 0) + R"(
  }
})";

        return status;

    } catch (const std::exception& e) {
        return R"({"error": "Failed to get status: )" + std::string(e.what()) + R"("})";
    }
}

} // namespace opcua2http
