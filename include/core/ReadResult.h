#pragma once

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace opcua2http {

/**
 * @brief Structure representing the result of reading an OPC UA node
 * 
 * This structure encapsulates all information returned when reading a data point
 * from an OPC UA server, including success status, value, and timestamp.
 */
struct ReadResult {
    std::string id;           // NodeId (OPC UA node identifier)
    bool success;             // Success status (s field in JSON)
    std::string reason;       // Status description (r field in JSON)
    std::string value;        // Read value as string (v field in JSON)
    uint64_t timestamp;       // Unix timestamp in milliseconds (t field in JSON)
    
    /**
     * @brief Convert ReadResult to JSON format
     * @return nlohmann::json object with standard API response format
     */
    nlohmann::json toJson() const {
        return nlohmann::json{
            {"id", id},
            {"s", success},
            {"r", reason},
            {"v", value},
            {"t", timestamp}
        };
    }
    
    /**
     * @brief Create ReadResult from JSON
     * @param j JSON object containing ReadResult data
     * @return ReadResult instance
     */
    static ReadResult fromJson(const nlohmann::json& j) {
        ReadResult result;
        j.at("id").get_to(result.id);
        j.at("s").get_to(result.success);
        j.at("r").get_to(result.reason);
        j.at("v").get_to(result.value);
        j.at("t").get_to(result.timestamp);
        return result;
    }
    
    /**
     * @brief Create a successful ReadResult
     * @param nodeId The OPC UA node identifier
     * @param value The read value as string
     * @param timestamp Unix timestamp in milliseconds
     * @return ReadResult with success=true
     */
    static ReadResult createSuccess(const std::string& nodeId, 
                                  const std::string& value, 
                                  uint64_t timestamp) {
        return ReadResult{
            nodeId,
            true,
            "Good",
            value,
            timestamp
        };
    }
    
    /**
     * @brief Create a failed ReadResult
     * @param nodeId The OPC UA node identifier
     * @param reason Error description
     * @param timestamp Unix timestamp in milliseconds
     * @return ReadResult with success=false
     */
    static ReadResult createError(const std::string& nodeId, 
                                const std::string& reason, 
                                uint64_t timestamp = 0) {
        return ReadResult{
            nodeId,
            false,
            reason,
            "",
            timestamp
        };
    }
};

} // namespace opcua2http

// Enable automatic JSON serialization for nlohmann::json
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(opcua2http::ReadResult, id, success, reason, value, timestamp)