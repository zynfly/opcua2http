#pragma once

#include <string>
#include <vector>

namespace opcua2http {

/**
 * @brief Forward declaration for BackgroundUpdater
 * 
 * This is a placeholder for the BackgroundUpdater component that will be
 * implemented in a future task. The ReadStrategy can work without it,
 * but background updates for stale cache entries will be skipped.
 */
class BackgroundUpdater {
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
    virtual ~BackgroundUpdater() = default;
};

} // namespace opcua2http