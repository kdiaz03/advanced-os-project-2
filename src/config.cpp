#include "config.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

uint32_t parseUnsigned(const std::string& text, const std::string& field) {
    uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        throw std::runtime_error(field + " must be an unsigned 32-bit integer: " + text);
    }
    return value;
}

const NodeIdentifier& findNode(const Configuration& config, uint32_t nodeId) {
    for (const auto& node : config.nodes) {
        if (node.nodeId == nodeId) return node;
    }
    throw std::runtime_error("Unknown node ID: " + std::to_string(nodeId));
}

namespace {
double parseMean(const std::string& text) {
    size_t consumed = 0;
    double value = 0;
    try {
        value = std::stod(text, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid timing mean: " + text);
    }
    if (consumed != text.size() || !std::isfinite(value) || value < 0) {
        throw std::runtime_error("Timing means must be finite and nonnegative: " + text);
    }
    return value;
}

void validateQuorums(const Configuration& config) {
    for (const auto& node : config.nodes) {
        std::set<uint32_t> members;
        for (uint32_t id : node.quorum) {
            findNode(config, id);
            if (!members.insert(id).second) throw std::runtime_error("Duplicate quorum member");
        }
        for (const auto& other : config.nodes) {
            const bool intersects = std::any_of(other.quorum.begin(), other.quorum.end(),
                [&](uint32_t id) { return members.count(id) != 0; });
            if (!intersects) throw std::runtime_error("Quorums must have pairwise intersections");
        }
    }
}
}

Configuration parseConfigurationFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open config: " + path);

    // Bound the input before parsing or allocating from its declared node count.
    std::string contents(100001, '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    contents.resize(static_cast<size_t>(input.gcount()));
    if (input.bad()) throw std::runtime_error("Cannot read config: " + path);
    if (contents.size() > 100000) throw std::runtime_error("Config exceeds 100 KB: " + path);

    Configuration config{};
    std::set<uint32_t> ids;
    std::set<std::pair<std::string, uint32_t>> endpoints;
    std::istringstream lines(contents);
    std::string line;
    size_t lineNumber = 0;
    size_t validLines = 0;
    while (std::getline(lines, line)) {
        ++lineNumber;
        line.erase(line.find('#') == std::string::npos ? line.size() : line.find('#'));
        std::istringstream words(line);
        std::vector<std::string> tokens;
        for (std::string token; words >> token;) tokens.push_back(token);
        if (tokens.empty() || tokens.front().find_first_not_of("0123456789") != std::string::npos) continue;

        try {
            if (validLines == 0) {
                if (tokens.size() != 4) throw std::runtime_error("Expected four header fields");
                config.params = {parseUnsigned(tokens[0], "Node count"), parseMean(tokens[1]),
                    parseMean(tokens[2]), parseUnsigned(tokens[3], "Request count")};
                if (config.params.numNodes == 0 || config.params.numNodes > contents.size() / 2) {
                    throw std::runtime_error("Node count cannot fit in this config");
                }
            } else if (validLines <= config.params.numNodes) {
                if (tokens.size() != 3) throw std::runtime_error("Expected node ID, hostname, port");
                const uint32_t id = parseUnsigned(tokens[0], "Node ID");
                const uint32_t port = parseUnsigned(tokens[2], "Port");
                if (port == 0 || port > 65535) throw std::runtime_error("Port must be in 1..65535");
                if (!ids.insert(id).second) throw std::runtime_error("Duplicate node ID");
                if (!endpoints.emplace(tokens[1], port).second) throw std::runtime_error("Duplicate endpoint");
                config.nodes.push_back({id, tokens[1], static_cast<uint16_t>(port), {}});
            } else if (validLines <= 2ULL * config.params.numNodes) {
                auto& quorum = config.nodes[validLines - config.params.numNodes - 1].quorum;
                for (const auto& token : tokens) quorum.push_back(parseUnsigned(token, "Quorum member"));
            } else {
                throw std::runtime_error("Unexpected additional config data");
            }
            ++validLines;
        } catch (const std::runtime_error& error) {
            throw std::runtime_error(path + ":" + std::to_string(lineNumber) + ": " + error.what());
        }
    }
    if (validLines == 0 || validLines != 1 + 2ULL * config.params.numNodes) {
        throw std::runtime_error(path + ": expected one header, n node records, and n quorum rows");
    }
    try {
        validateQuorums(config);
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(path + ": " + error.what());
    }
    return config;
}
