#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

// the four header values; both timing means are in milliseconds.
struct ConfigurationParams {
    uint32_t numNodes;
    double meanInterRequestMs;
    double meanCsMs;
    uint32_t requestsPerNode;
};

// a configured endpoint and the supplied quorum for its requests.
struct NodeIdentifier {
    uint32_t nodeId;
    std::string hostName;
    uint16_t port;
    std::vector<uint32_t> quorum;
};

struct Configuration {
    ConfigurationParams params;
    // preserve node-record order, including when ids are out of order.
    std::vector<NodeIdentifier> nodes;
};

uint32_t parseUnsigned(const std::string& text, const std::string& field);
Configuration parseConfigurationFile(const std::string& path);
const NodeIdentifier& findNode(const Configuration& config, uint32_t nodeId);

#endif
