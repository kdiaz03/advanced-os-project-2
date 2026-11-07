#include "config.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    try {
        std::string configPath;
        std::optional<uint32_t> nodeId;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "-c" && i + 1 < argc && configPath.empty()) configPath = argv[++i];
            else if (option == "-n" && i + 1 < argc && !nodeId) nodeId = parseUnsigned(argv[++i], "Node ID");
            else throw std::runtime_error("Usage: ./build/main -c <config> -n <node_id>");
        }
        if (configPath.empty() || !nodeId) throw std::runtime_error("Usage: ./build/main -c <config> -n <node_id>");
        const auto config = parseConfigurationFile(configPath);
        const auto& node = findNode(config, *nodeId);
        std::cout << "Config valid: node " << node.nodeId << " at " << node.hostName << ':' << node.port
                  << ", " << config.params.numNodes << " nodes, quorum:";
        for (uint32_t member : node.quorum) std::cout << ' ' << member;
        std::cout << "\nMutex service is not implemented yet.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
