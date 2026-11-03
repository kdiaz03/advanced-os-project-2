//
//  config.cpp
//  Project 2 - Advanced Operating Systems
//
//  Created by Keven Diaz on 11/02/2026
//

#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.hpp"

SystemConfiguration parseConfigFile(std::string filepath) {
    std::ifstream configFile(filepath);
    if( !configFile.is_open() ) {
        throw std::runtime_error("Failed to open the configuration file " + filepath);
    }

    SystemConfiguration systemConfig = {};

    bool systemParamsFilled = false;
    std::vector<bool> nodeDefined;
    uint32_t nodeLinesRead = 0;
    uint32_t quorumLinesRead = 0;
    uint32_t lineNum = 0;
    std::string line;

    auto fail = [&](const std::string& message) {
        throw std::runtime_error(filepath + ":" + std::to_string(lineNum) + ":" + message);
    };

    while(std::getline(configFile, line)) {
        lineNum++;

        // cut the string once we find a '#' char
        std::size_t pos = line.find('#');
        if(pos != std::string::npos) {
            line.erase(pos);
        }

        // find the first non-space char
        std::size_t nonSpaceIdx = std::string::npos;
        for( std::size_t i = 0; i < line.length(); i++ ) {
            if( std::isspace( static_cast<unsigned char>(line[i])) ) {
                continue;
            } else {
                nonSpaceIdx = i;
                break;
            }
        }
        if(nonSpaceIdx == std::string::npos) {
            continue;
        }

        std::istringstream iss( line );
        std::string token;
        std::vector<std::string> tokens;
        while( iss >> token ) {
            tokens.push_back( token );
        }
        if( tokens.empty() ) {
            continue;
        }
        if( tokens[0].find_first_not_of("0123456789") != std::string::npos ) {
            continue;
        }

        if( !systemParamsFilled ) {
            // the first valid line may only consist of 4 tokens
            if( tokens.size() != 4 ) {
                fail("Expected exactly 4 tokens");
            }

            uint32_t systemVals[4];
            for( std::size_t i = 0; i < tokens.size(); i++ ) {
                systemVals[i] = static_cast<uint32_t>(std::stoul(tokens[i]));
            }
            systemConfig.numNodes = systemVals[0];
            systemConfig.interRequestDelay = systemVals[1];
            systemConfig.csExecutionTime = systemVals[2];
            systemConfig.requestCount = systemVals[3];

            systemConfig.nodes.resize(systemConfig.numNodes);
            nodeDefined.resize(systemConfig.numNodes, false);

            systemParamsFilled = true;
            continue;
        }

        // following n qualifying nodes define each node
        if( nodeLinesRead < systemConfig.numNodes ) {
            if( tokens.size() != 3 ) {
                fail("Expected 3 tokens: node ID, hosname, and port");
            }

            uint32_t nodeID = static_cast<uint32_t>( std::stoul(tokens[0]) );
            uint32_t port = static_cast<uint32_t>( std::stoul(tokens[2]) );

            if( nodeID >= systemConfig.numNodes ) {
                fail("Node ID must be less than numNodes");
            }

            if( nodeDefined[nodeID] ) {
                fail("Duplicate node definition for " + std::to_string(nodeID));
            }

            if( port == 0 || port > std::numeric_limits<uint16_t>::max() ) {
                fail("Listening port must be in a valid range");
            }

            NodeIdentifier& node = systemConfig.nodes[nodeID];
            node.nodeID = nodeID;
            node.hostName = tokens[1];
            node.port = static_cast<uint16_t>(port);

            nodeDefined[nodeID] = true;
            nodeLinesRead++;
            continue;
        }

        // final n given lines should define quorums for nodes 0 - n - 1
        if( quorumLinesRead < systemConfig.numNodes ) {
            NodeIdentifier& node = systemConfig.nodes[ quorumLinesRead ];

            for( const auto& quorumToken : tokens ) {
                uint32_t quorumID = static_cast<uint32_t>(std::stoul(quorumToken));
                if( quorumID >= systemConfig.numNodes ) {
                    fail("Neighbor ID must be less than numNodes");
                }

                node.quorum.push_back(quorumID);
            }

            quorumLinesRead++;
            continue;
        }
    }

    if( !systemParamsFilled ) {
        fail("Missing system parameters");
    }
    if( nodeLinesRead != systemConfig.numNodes ) {
        fail("Missing node definitions");
    }
    if( quorumLinesRead != systemConfig.numNodes ) {
        fail("Missing quorum definitions");
    }

    return systemConfig;

}