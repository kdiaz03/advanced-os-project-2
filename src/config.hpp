#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

struct ConfigurationParams {
    uint32_t numNodes;
    double meanInterRequestMs;
    double meanCsMs;
    uint32_t requestsPerNode;
};

struct NodeIdentifier {
    uint32_t nodeId;
    std::string hostName;
    uint16_t port;
    std::vector<uint32_t> quorum;
};

struct Configuration {
    ConfigurationParams params;
    std::vector<NodeIdentifier> nodes;
};

uint32_t parseUnsigned(const std::string& text, const std::string& field);
Configuration parseConfigurationFile(const std::string& path);
const NodeIdentifier& findNode(const Configuration& config, uint32_t nodeId);

#endif
