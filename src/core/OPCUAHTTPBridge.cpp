#include "core/OPCUAHTTPBridge.h"
#include <iostream>
#include <csignal>
#include <crow.h>

namespace opcua2http {

// Static instance for signal handling
OPCUAHTTPBridge* OPCUAHTTPBridge::instance_ = nullptr;

OPCUAHTTPBridge::OPCUAHTTPBridge() 
    : running_(false), app_(nullptr) {
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
    std::cout << "Initializing OPC UA HTTP Bridge..." << std::endl;
    
    // Initialize configuration
    if (!initializeConfiguration()) {
        std::cerr << "Failed to initialize configuration" << std::endl;
        return false;
    }
    
    std::cout << "Configuration loaded successfully" << std::endl;
    std::cout << config_->toString() << std::endl;
    
    // Setup signal handlers for graceful shutdown
    setupSignalHandlers();
    
    // Initialize OPC UA client (placeholder for now)
    if (!initializeOPCClient()) {
        std::cerr << "Failed to initialize OPC UA client" << std::endl;
        return false;
    }
    
    // Initialize other components (placeholder for now)
    if (!initializeComponents()) {
        std::cerr << "Failed to initialize components" << std::endl;
        return false;
    }
    
    // Setup HTTP server
    if (!setupHTTPServer()) {
        std::cerr << "Failed to setup HTTP server" << std::endl;
        return false;
    }
    
    std::cout << "OPC UA HTTP Bridge initialized successfully" << std::endl;
    return true;
}

void OPCUAHTTPBridge::run() {
    if (!app_) {
        std::cerr << "HTTP server not initialized" << std::endl;
        return;
    }
    
    running_.store(true);
    
    std::cout << "Starting HTTP server on port " << config_->serverPort << std::endl;
    
    try {
        // Cast back to crow::SimpleApp and start the server (this blocks)
        auto* crowApp = static_cast<crow::SimpleApp*>(app_);
        crowApp->port(static_cast<uint16_t>(config_->serverPort)).multithreaded().run();
    } catch (const std::exception& e) {
        std::cerr << "Error running HTTP server: " << e.what() << std::endl;
    }
    
    running_.store(false);
}

void OPCUAHTTPBridge::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "Stopping OPC UA HTTP Bridge..." << std::endl;
    
    running_.store(false);
    
    // Stop HTTP server
    if (app_) {
        auto* crowApp = static_cast<crow::SimpleApp*>(app_);
        crowApp->stop();
    }
    
    // Wait for server thread if it's running
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    
    std::cout << "OPC UA HTTP Bridge stopped" << std::endl;
}

bool OPCUAHTTPBridge::initializeConfiguration() {
    try {
        config_ = std::make_unique<Configuration>(Configuration::loadFromEnvironment());
        return config_->validate();
    } catch (const std::exception& e) {
        std::cerr << "Error loading configuration: " << e.what() << std::endl;
        return false;
    }
}

bool OPCUAHTTPBridge::initializeOPCClient() {
    // Placeholder - will be implemented in later tasks
    std::cout << "OPC UA client initialization (placeholder)" << std::endl;
    return true;
}

bool OPCUAHTTPBridge::initializeComponents() {
    // Placeholder - will be implemented in later tasks
    std::cout << "Components initialization (placeholder)" << std::endl;
    return true;
}

bool OPCUAHTTPBridge::setupHTTPServer() {
    try {
        // Create Crow app and store as void pointer
        auto* crowApp = new crow::SimpleApp();
        app_ = crowApp;
        
        // Basic health check endpoint
        CROW_ROUTE((*crowApp), "/health")
        ([this]() {
            return crow::response(200, "application/json", 
                R"({"status":"ok","service":"opcua-http-bridge"})");
        });
        
        // Placeholder for main API endpoint
        CROW_ROUTE((*crowApp), "/iotgateway/read")
        .methods("GET"_method)
        ([this](const crow::request& /*req*/) {
            return crow::response(501, "application/json", 
                R"({"error":"Not implemented yet"})");
        });
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error setting up HTTP server: " << e.what() << std::endl;
        return false;
    }
}

void OPCUAHTTPBridge::signalHandler(int signal) {
    if (instance_) {
        std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
        instance_->stop();
    }
}

void OPCUAHTTPBridge::setupSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
}

void OPCUAHTTPBridge::cleanup() {
    // Cleanup will be expanded as more components are added
    std::cout << "Cleaning up resources..." << std::endl;
    
    // Clean up Crow app
    if (app_) {
        delete static_cast<crow::SimpleApp*>(app_);
        app_ = nullptr;
    }
}

} // namespace opcua2http