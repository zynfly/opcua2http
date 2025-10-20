#pragma once

#include <string>
#include <vector>

namespace opcua2http {

/**
 * @brief Interface for background updater components
 * 
 * This interface allows for dependency injection and testing with mock implementations.
 */
class IBackgroundUpdater {
public:
    /**
     * @brief Schedule background update for a single node
     * @param nodeId Node identifier to update
     */
    virtual void scheduleUpdate(const std::string& nodeId) = 0;

    /**
     * @brief Schedule background updates for multiple nodes
     * @param nodeIds Vector of node identifiers to update
     */
    virtual void scheduleBatchUpdate(const std::vector<std::string>& nodeIds) = 0;

    /**
     * @brief Virtual destructor
     */
    virtual ~IBackgroundUpdater() = default;
};

} // namespace opcua2http