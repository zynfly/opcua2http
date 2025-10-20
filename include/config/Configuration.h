#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace opcua2http {

/**
 * @brief Configuration structure for the OPC UA HTTP Bridge
 * 
 * This structure holds all configuration parameters loaded from environment variables.
 * It supports OPC UA connection settings, HTTP server configuration, security options,
 * and operational parameters.
 */
struct Configuration {
    // OPC UA Configuration
    std::string opcEndpoint;              // OPC_ENDPOINT
    int securityMode;                     // OPC_SECURITY_MODE (1=None, 2=Sign, 3=SignAndEncrypt)
    std::string securityPolicy;           // OPC_SECURITY_POLICY
    int defaultNamespace;                 // OPC_NAMESPACE
    std::string applicationUri;           // OPC_APPLICATION_URI
    
    // Connection Configuration
    int connectionRetryMax;               // CONNECTION_RETRY_MAX
    int connectionInitialDelay;           // CONNECTION_INITIAL_DELAY
    int connectionMaxRetry;               // CONNECTION_MAX_RETRY
    int connectionMaxDelay;               // CONNECTION_MAX_DELAY
    int connectionRetryDelay;             // CONNECTION_RETRY_DELAY
    
    // Web Server Configuration
    int serverPort;                       // SERVER_PORT
    
    // Security Configuration
    std::string apiKey;                   // API_KEY
    std::string authUsername;             // AUTH_USERNAME
    std::string authPassword;             // AUTH_PASSWORD
    std::vector<std::string> allowedOrigins; // ALLOWED_ORIGINS (comma-separated)
    
    // Cache Configuration (Legacy - for backward compatibility)
    int cacheExpireMinutes;               // CACHE_EXPIRE_MINUTES
    int subscriptionCleanupMinutes;       // SUBSCRIPTION_CLEANUP_MINUTES
    
    // New Cache Timing Configuration
    int cacheRefreshThresholdSeconds;     // CACHE_REFRESH_THRESHOLD_SECONDS
    int cacheExpireSeconds;               // CACHE_EXPIRE_SECONDS
    int cacheCleanupIntervalSeconds;      // CACHE_CLEANUP_INTERVAL_SECONDS
    
    // Background Update Configuration
    int backgroundUpdateThreads;         // BACKGROUND_UPDATE_THREADS
    int backgroundUpdateQueueSize;       // BACKGROUND_UPDATE_QUEUE_SIZE
    int backgroundUpdateTimeoutMs;       // BACKGROUND_UPDATE_TIMEOUT_MS
    
    // Performance Tuning Configuration
    int cacheMaxEntries;                 // CACHE_MAX_ENTRIES
    int cacheMaxMemoryMb;                // CACHE_MAX_MEMORY_MB
    int cacheConcurrentReads;            // CACHE_CONCURRENT_READS
    
    // OPC UA Optimization Configuration
    int opcReadTimeoutMs;                // OPC_READ_TIMEOUT_MS
    int opcBatchSize;                    // OPC_BATCH_SIZE
    int opcConnectionPoolSize;           // OPC_CONNECTION_POOL_SIZE
    
    // Logging Configuration
    std::string logLevel;                 // LOG_LEVEL
    
    /**
     * @brief Load configuration from environment variables
     * @return Configuration instance with values from environment or defaults
     */
    static Configuration loadFromEnvironment();
    
    /**
     * @brief Validate configuration parameters
     * @return true if configuration is valid, false otherwise
     */
    bool validate() const;
    
    /**
     * @brief Validate cache timing configuration parameters
     * @return true if cache timing configuration is valid, false otherwise
     */
    bool validateCacheTimingConfig() const;
    
    /**
     * @brief Validate performance configuration parameters
     * @return true if performance configuration is valid, false otherwise
     */
    bool validatePerformanceConfig() const;
    
    /**
     * @brief Load cache-specific settings from environment variables
     */
    void loadCacheSettings();
    
    /**
     * @brief Get configuration as string for logging
     * @return String representation of configuration (sensitive data masked)
     */
    std::string toString() const;
    
private:
    /**
     * @brief Get environment variable as string with default value
     * @param name Environment variable name
     * @param defaultValue Default value if variable is not set
     * @return Environment variable value or default
     */
    static std::string getEnvString(const std::string& name, const std::string& defaultValue = "");
    
    /**
     * @brief Get environment variable as integer with default value
     * @param name Environment variable name
     * @param defaultValue Default value if variable is not set or invalid
     * @return Environment variable value as integer or default
     */
    static int getEnvInt(const std::string& name, int defaultValue = 0);
    
    /**
     * @brief Parse comma-separated string into vector
     * @param value Comma-separated string
     * @return Vector of trimmed strings
     */
    static std::vector<std::string> parseCommaSeparated(const std::string& value);
};

} // namespace opcua2http