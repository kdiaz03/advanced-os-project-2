//
//  config.hpp
//  Project 2 - Advanced Operating Systems
//
//  Created by Keven Diaz on 11/02/2026
//

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

struct NodeIdentifier {
    uint32_t                nodeID;
    std::string             hostName;
    uint16_t                port;
    std::vector<uint32_t>   quorum;
};

struct SystemConfiguration {
    // first four tokens capturing system parameters
    uint32_t numNodes;
    uint32_t interRequestDelay; // mean value in milliseconds
    uint32_t csExecutionTime;   // mean value in milliseconds
    uint32_t requestCount;

    std::vector<NodeIdentifier> nodes;
};

SystemConfiguration parseConfigFile(std::string filepath);

#endif