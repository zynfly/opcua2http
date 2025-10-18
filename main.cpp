/**
 * @file main.cpp
 * @brief OPC UA to HTTP Bridge - Main Entry Point
 * 
 * This application bridges OPC UA servers to HTTP REST API, providing:
 * - Intelligent caching with automatic subscription management
 * - Automatic reconnection and recovery
 * - RESTful API for reading OPC UA data
 * - Configurable security and authentication
 * 
 * @author OPC UA HTTP Bridge Team
 * @version 1.0.0
 */

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <spdlog/spdlog.h>
#include "core/OPCUAHTTPBridge.h"

// Application version information
constexpr const char* APP_VERSION = "1.0.0";
constexpr const char* APP_NAME = "OPC UA HTTP Bridge";

/**
 * @brief Print application banner
 */
void printBanner() {
    std::cout << R"(
╔═══════════════════════════════════════════════════════════╗
║                                                           ║
║           OPC UA to HTTP Bridge v)" << APP_VERSION << R"(                ║
║                                                           ║
║  Intelligent caching • Auto-reconnection • RESTful API   ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝
)" << std::endl;
}

/**
 * @brief Print usage information
 */
void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help              Show this help message and exit\n";
    std::cout << "  -v, --version           Show version information and exit\n";
    std::cout << "  -c, --config            Show configuration information and exit\n";
    std::cout << "  -d, --debug             Enable debug logging\n";
    std::cout << "  -q, --quiet             Suppress non-error output\n";
    std::cout << "  --log-level LEVEL       Set log level (trace, debug, info, warn, error, critical)\n";
    std::cout << "\nConfiguration:\n";
    std::cout << "  All configuration is done via environment variables.\n";
    std::cout << "  See documentation for available environment variables.\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << programName << "                    # Start with default settings\n";
    std::cout << "  " << programName << " --debug            # Start with debug logging\n";
    std::cout << "  " << programName << " --log-level trace  # Start with trace logging\n";
    std::cout << "\nEnvironment Variables (Key Configuration):\n";
    std::cout << "  OPC_ENDPOINT            OPC UA server endpoint (required)\n";
    std::cout << "  SERVER_PORT             HTTP server port (default: 3000)\n";
    std::cout << "  API_KEY                 API key for authentication (optional)\n";
    std::cout << "  AUTH_USERNAME           Basic auth username (optional)\n";
    std::cout << "  AUTH_PASSWORD           Basic auth password (optional)\n";
    std::cout << "\nFor full configuration options, see README.md or documentation.\n";
    std::cout << std::endl;
}

/**
 * @brief Print version information
 */
void printVersion() {
    std::cout << APP_NAME << " version " << APP_VERSION << "\n";
    std::cout << "Built with:\n";
    std::cout << "  - open62541 (OPC UA client library)\n";
    std::cout << "  - Crow (HTTP web framework)\n";
    std::cout << "  - nlohmann-json (JSON processing)\n";
    std::cout << "  - spdlog (Logging framework)\n";
    std::cout << std::endl;
}

/**
 * @brief Print configuration help
 */
void printConfigHelp() {
    std::cout << "Configuration Environment Variables:\n\n";
    std::cout << "=== Core OPC UA Configuration ===\n";
    std::cout << "  OPC_ENDPOINT              OPC UA Server URL (e.g., opc.tcp://127.0.0.1:4840)\n";
    std::cout << "  OPC_SECURITY_MODE         Security mode: 1=None, 2=Sign, 3=SignAndEncrypt\n";
    std::cout << "  OPC_SECURITY_POLICY       Security policy: None, Basic256Sha256, etc.\n";
    std::cout << "  OPC_NAMESPACE             Default namespace for Node IDs (default: 2)\n";
    std::cout << "  OPC_APPLICATION_URI       Client application URI\n\n";
    
    std::cout << "=== Connection Configuration ===\n";
    std::cout << "  CONNECTION_RETRY_MAX      Max retries per connection attempt (default: 5)\n";
    std::cout << "  CONNECTION_INITIAL_DELAY  Initial delay before first attempt in ms (default: 1000)\n";
    std::cout << "  CONNECTION_MAX_RETRY      Global max reconnection attempts, -1=infinite (default: 10)\n";
    std::cout << "  CONNECTION_MAX_DELAY      Max delay between retries in ms (default: 10000)\n";
    std::cout << "  CONNECTION_RETRY_DELAY    Base delay between retries in ms (default: 5000)\n\n";
    
    std::cout << "=== Web Server Configuration ===\n";
    std::cout << "  SERVER_PORT               HTTP server port (default: 3000)\n\n";
    
    std::cout << "=== Security Configuration ===\n";
    std::cout << "  API_KEY                   Secret key for X-API-Key authentication\n";
    std::cout << "  AUTH_USERNAME             Username for Basic Authentication\n";
    std::cout << "  AUTH_PASSWORD             Password for Basic Authentication\n";
    std::cout << "  ALLOWED_ORIGINS           Comma-separated list of allowed CORS origins\n\n";
    
    std::cout << "=== Cache & Subscription Configuration ===\n";
    std::cout << "  CACHE_EXPIRE_MINUTES      Cache expiration time in minutes (default: 60)\n";
    std::cout << "  SUBSCRIPTION_CLEANUP_MIN  Subscription cleanup interval in minutes (default: 30)\n\n";
    
    std::cout << "=== Logging Configuration ===\n";
    std::cout << "  LOG_LEVEL                 Log level: trace, debug, info, warn, error, critical\n";
    std::cout << std::endl;
}

/**
 * @brief Parse command line arguments
 * @return 0 to continue, 1 to exit successfully, -1 to exit with error
 */
int parseArguments(int argc, char* argv[], spdlog::level::level_enum& logLevel, bool& showBanner) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 1;
        }
        else if (arg == "-v" || arg == "--version") {
            printVersion();
            return 1;
        }
        else if (arg == "-c" || arg == "--config") {
            printConfigHelp();
            return 1;
        }
        else if (arg == "-d" || arg == "--debug") {
            logLevel = spdlog::level::debug;
        }
        else if (arg == "-q" || arg == "--quiet") {
            showBanner = false;
            logLevel = spdlog::level::warn;
        }
        else if (arg == "--log-level") {
            if (i + 1 < argc) {
                std::string level = argv[++i];
                if (level == "trace") logLevel = spdlog::level::trace;
                else if (level == "debug") logLevel = spdlog::level::debug;
                else if (level == "info") logLevel = spdlog::level::info;
                else if (level == "warn") logLevel = spdlog::level::warn;
                else if (level == "error") logLevel = spdlog::level::err;
                else if (level == "critical") logLevel = spdlog::level::critical;
                else {
                    std::cerr << "Error: Invalid log level '" << level << "'\n";
                    std::cerr << "Valid levels: trace, debug, info, warn, error, critical\n";
                    return -1;
                }
            } else {
                std::cerr << "Error: --log-level requires an argument\n";
                return -1;
            }
        }
        else {
            std::cerr << "Error: Unknown option '" << arg << "'\n";
            std::cerr << "Use --help for usage information\n";
            return -1;
        }
    }
    
    return 0; // Continue execution
}

/**
 * @brief Main entry point
 */
int main(int argc, char* argv[]) {
    // Default settings
    spdlog::level::level_enum logLevel = spdlog::level::info;
    bool showBanner = true;
    
    // Parse command line arguments
    int parseResult = parseArguments(argc, argv, logLevel, showBanner);
    if (parseResult != 0) {
        return (parseResult > 0) ? 0 : 1;
    }
    
    // Configure logging
    spdlog::set_level(logLevel);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    
    // Show banner if not suppressed
    if (showBanner) {
        printBanner();
    }
    
    try {
        // Create the bridge application
        spdlog::info("Creating OPC UA HTTP Bridge instance...");
        opcua2http::OPCUAHTTPBridge bridge;
        
        // Initialize all components
        spdlog::info("Initializing bridge components...");
        if (!bridge.initialize()) {
            spdlog::critical("Failed to initialize OPC UA HTTP Bridge");
            std::cerr << "\nFailed to initialize OPC UA HTTP Bridge" << std::endl;
            std::cerr << "Check the logs above for details." << std::endl;
            std::cerr << "Common issues:" << std::endl;
            std::cerr << "  - OPC_ENDPOINT not set or unreachable" << std::endl;
            std::cerr << "  - Invalid security configuration" << std::endl;
            std::cerr << "  - Port already in use" << std::endl;
            std::cerr << "\nUse --config to see all configuration options." << std::endl;
            return 1;
        }
        
        spdlog::info("Initialization complete");
        
        if (showBanner) {
            std::cout << "\n✓ Bridge initialized successfully" << std::endl;
            std::cout << "✓ Press Ctrl+C to shutdown gracefully\n" << std::endl;
        }
        
        // Run the server (this blocks until shutdown)
        bridge.run();
        
        spdlog::info("OPC UA HTTP Bridge shutdown complete");
        
        if (showBanner) {
            std::cout << "\n✓ Shutdown complete" << std::endl;
        }
        
    } catch (const std::exception& e) {
        spdlog::critical("Fatal error: {}", e.what());
        std::cerr << "\nFatal error: " << e.what() << std::endl;
        std::cerr << "The application will now exit." << std::endl;
        return 1;
    } catch (...) {
        spdlog::critical("Unknown fatal error occurred");
        std::cerr << "\nUnknown fatal error occurred" << std::endl;
        std::cerr << "The application will now exit." << std::endl;
        return 1;
    }
    
    return 0;
}