#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <iomanip>
#include <sstream>
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
    bool success;             // Success status
    std::string reason;       // Status description (used as quality)
    std::string value;        // Read value as string
    uint64_t timestamp;       // Unix timestamp in milliseconds

    /**
     * @brief Convert ReadResult to JSON format with full field names
     * @return nlohmann::json object with standard API response format
     */
    nlohmann::json toJson() const {
        // Format timestamp as ISO 8601 string
        auto timePoint = std::chrono::system_clock::from_time_t(timestamp / 1000);
        auto ms = timestamp % 1000;
        std::time_t time = std::chrono::system_clock::to_time_t(timePoint);

#ifdef _WIN32
        std::tm tm_buf;
        gmtime_s(&tm_buf, &time);
        std::tm* tm = &tm_buf;
#else
        std::tm* tm = std::gmtime(&time);
#endif

        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
        oss << "." << std::setfill('0') << std::setw(3) << ms << "Z";

        return nlohmann::json{
            {"nodeId", id},
            {"success", success},
            {"quality", reason},
            {"value", value},
            {"timestamp_iso", oss.str()}
        };
    }

    /**
     * @brief Create ReadResult from JSON (supports both old and new formats)
     * @param j JSON object containing ReadResult data
     * @return ReadResult instance
     */
    static ReadResult fromJson(const nlohmann::json& j) {
        ReadResult result;

        // Support both old format (id, s, r, v, t) and new format (nodeId, success, quality, value, timestamp_iso)
        if (j.contains("nodeId")) {
            // New format
            j.at("nodeId").get_to(result.id);
            j.at("success").get_to(result.success);
            j.at("quality").get_to(result.reason);
            j.at("value").get_to(result.value);

            // Handle timestamp - could be timestamp_iso string or numeric timestamp
            if (j.contains("timestamp_iso")) {
                // For now, just use current timestamp - proper ISO parsing would need more complex code
                result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            } else if (j.contains("timestamp")) {
                j.at("timestamp").get_to(result.timestamp);
            } else {
                result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
            }
        } else {
            // Old format (for backward compatibility)
            j.at("id").get_to(result.id);
            j.at("s").get_to(result.success);
            j.at("r").get_to(result.reason);
            j.at("v").get_to(result.value);
            j.at("t").get_to(result.timestamp);
        }

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

// Note: We use custom toJson() and fromJson() methods instead of automatic serialization
// to support both old and new JSON formats and proper timestamp formatting
