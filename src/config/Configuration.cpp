#include "config/Configuration.h"
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace opcua2http {

Configuration Configuration::loadFromEnvironment() {
    Configuration config;
    
    // OPC UA Configuration
    config.opcEndpoint = getEnvString("OPC_ENDPOINT", "opc.tcp://localhost:4840");
    config.securityMode = getEnvInt("OPC_SECURITY_MODE", 1); // 1 = None
    config.securityPolicy = getEnvString("OPC_SECURITY_POLICY", "None");
    config.defaultNamespace = getEnvInt("OPC_NAMESPACE", 2);
    config.applicationUri = getEnvString("OPC_APPLICATION_URI", "urn:opcua2http:client");
    
    // Connection Configuration
    config.connectionRetryMax = getEnvInt("CONNECTION_RETRY_MAX", 5);
    config.connectionInitialDelay = getEnvInt("CONNECTION_INITIAL_DELAY", 1000);
    config.connectionMaxRetry = getEnvInt("CONNECTION_MAX_RETRY", 10);
    config.connectionMaxDelay = getEnvInt("CONNECTION_MAX_DELAY", 30000);
    config.connectionRetryDelay = getEnvInt("CONNECTION_RETRY_DELAY", 5000);
    
    // Web Server Configuration
    config.serverPort = getEnvInt("SERVER_PORT", 3000);
    
    // Security Configuration
    config.apiKey = getEnvString("API_KEY");
    config.authUsername = getEnvString("AUTH_USERNAME");
    config.authPassword = getEnvString("AUTH_PASSWORD");
    
    std::string allowedOriginsStr = getEnvString("ALLOWED_ORIGINS");
    if (!allowedOriginsStr.empty()) {
        config.allowedOrigins = parseCommaSeparated(allowedOriginsStr);
    }
    
    // Cache Configuration
    config.cacheExpireMinutes = getEnvInt("CACHE_EXPIRE_MINUTES", 60);
    config.subscriptionCleanupMinutes = getEnvInt("SUBSCRIPTION_CLEANUP_MINUTES", 30);
    
    // Logging Configuration
    config.logLevel = getEnvString("LOG_LEVEL", "INFO");
    
    return config;
}

bool Configuration::validate() const {
    // Validate required parameters
    if (opcEndpoint.empty()) {
        std::cerr << "Error: OPC_ENDPOINT is required" << std::endl;
        return false;
    }
    
    if (serverPort <= 0 || serverPort > 65535) {
        std::cerr << "Error: SERVER_PORT must be between 1 and 65535" << std::endl;
        return false;
    }
    
    if (securityMode < 1 || securityMode > 3) {
        std::cerr << "Error: OPC_SECURITY_MODE must be 1 (None), 2 (Sign), or 3 (SignAndEncrypt)" << std::endl;
        return false;
    }
    
    if (connectionRetryMax < 0) {
        std::cerr << "Error: CONNECTION_RETRY_MAX must be non-negative" << std::endl;
        return false;
    }
    
    if (connectionInitialDelay < 0) {
        std::cerr << "Error: CONNECTION_INITIAL_DELAY must be non-negative" << std::endl;
        return false;
    }
    
    if (cacheExpireMinutes <= 0) {
        std::cerr << "Error: CACHE_EXPIRE_MINUTES must be positive" << std::endl;
        return false;
    }
    
    if (subscriptionCleanupMinutes <= 0) {
        std::cerr << "Error: SUBSCRIPTION_CLEANUP_MINUTES must be positive" << std::endl;
        return false;
    }
    
    // Validate authentication configuration
    if (!authUsername.empty() && authPassword.empty()) {
        std::cerr << "Warning: AUTH_USERNAME provided but AUTH_PASSWORD is empty" << std::endl;
    }
    
    return true;
}

std::string Configuration::toString() const {
    std::ostringstream oss;
    oss << "Configuration:\n";
    oss << "  OPC UA Endpoint: " << opcEndpoint << "\n";
    oss << "  Security Mode: " << securityMode << "\n";
    oss << "  Security Policy: " << securityPolicy << "\n";
    oss << "  Default Namespace: " << defaultNamespace << "\n";
    oss << "  Application URI: " << applicationUri << "\n";
    oss << "  Server Port: " << serverPort << "\n";
    oss << "  Connection Retry Max: " << connectionRetryMax << "\n";
    oss << "  Connection Initial Delay: " << connectionInitialDelay << "ms\n";
    oss << "  Connection Max Retry: " << connectionMaxRetry << "\n";
    oss << "  Connection Max Delay: " << connectionMaxDelay << "ms\n";
    oss << "  Connection Retry Delay: " << connectionRetryDelay << "ms\n";
    oss << "  Cache Expire Minutes: " << cacheExpireMinutes << "\n";
    oss << "  Subscription Cleanup Minutes: " << subscriptionCleanupMinutes << "\n";
    oss << "  Log Level: " << logLevel << "\n";
    
    // Security info (masked)
    oss << "  API Key: " << (apiKey.empty() ? "not set" : "***") << "\n";
    oss << "  Auth Username: " << (authUsername.empty() ? "not set" : authUsername) << "\n";
    oss << "  Auth Password: " << (authPassword.empty() ? "not set" : "***") << "\n";
    
    if (!allowedOrigins.empty()) {
        oss << "  Allowed Origins: ";
        for (size_t i = 0; i < allowedOrigins.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << allowedOrigins[i];
        }
        oss << "\n";
    }
    
    return oss.str();
}

std::string Configuration::getEnvString(const std::string& name, const std::string& defaultValue) {
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : defaultValue;
}

int Configuration::getEnvInt(const std::string& name, int defaultValue) {
    const char* value = std::getenv(name.c_str());
    if (!value) {
        return defaultValue;
    }
    
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        std::cerr << "Warning: Invalid integer value for " << name << ": " << value 
                  << ", using default: " << defaultValue << std::endl;
        return defaultValue;
    }
}

std::vector<std::string> Configuration::parseCommaSeparated(const std::string& value) {
    std::vector<std::string> result;
    std::stringstream ss(value);
    std::string item;
    
    while (std::getline(ss, item, ',')) {
        // Trim whitespace
        item.erase(item.begin(), std::find_if(item.begin(), item.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        item.erase(std::find_if(item.rbegin(), item.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), item.end());
        
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    
    return result;
}

} // namespace opcua2http