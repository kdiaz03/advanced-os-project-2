#include "config.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
const std::string fixture = "build/config-test.txt";

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Configuration parse(const std::string& text) {
    { std::ofstream file(fixture); file << text; }
    return parseConfigurationFile(fixture);
}

void rejects(const std::string& text, const std::string& expected) {
    try { parse(text); }
    catch (const std::runtime_error& error) {
        require(std::string(error.what()).find(expected) != std::string::npos,
                "Wrong diagnostic: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("Accepted invalid input: " + expected);
}
}

int main() {
    try {
        const auto sample = parseConfigurationFile("configs/assignment-example.txt");
        require(sample.nodes.size() == 9 && sample.nodes[1].quorum.front() == 0, "Sample row mapping");
        const auto config = parse("  # comment\r\n% heading\n2 2.5 0 0 # header\n"
            "9 localhost 24009\nignored\n4 localhost 24004\n4 9 # row for 9\n4\ntrailer\n");
        require(config.params.meanInterRequestMs == 2.5 && config.params.meanCsMs == 0, "Timing means");
        require(findNode(config, 9).quorum == std::vector<uint32_t>({4, 9}), "Record-order mapping");
        require(findNode(config, 4).quorum == std::vector<uint32_t>({4}), "Unequal quorums");
        parse("1 1 1 1\n0 localhost 24000\n0");
        rejects("", "expected one header");
        rejects("1 1 1\n", "four header");
        rejects("0 1 1 1\n", "Node count");
        rejects("4294967296 1 1 1\n", "unsigned 32-bit");
        rejects("1 nan 1 1\n0 localhost 1\n0\n", "finite");
        rejects("1 -1 1 1\n0 localhost 1\n0\n", "nonnegative");
        rejects("1 1ms 1 1\n0 localhost 1\n0\n", "finite");
        rejects("1 1 1 1\n0 localhost 65536\n0\n", "Port");
        rejects("1 1 1 1\n0 localhost 0\n0\n", "Port");
        rejects("1 1 1 1\n0 localhost 1\n", "expected one header");
        rejects("1 1 1 1\n0 localhost 1\n0\n0\n", "additional");
        rejects("1 1 1 1\n0 localhost 1\n0 0\n", "Duplicate quorum");
        rejects("1 1 1 1\n0 localhost 1\n2\n", "Unknown node");
        rejects("1 1 1 1\n0 localhost 1\n0 nope\n", "unsigned 32-bit");
        rejects("2 1 1 1\n0 localhost 1\n0 localhost 2\n0\n0\n", "Duplicate node");
        rejects("2 1 1 1\n0 localhost 1\n1 localhost 1\n0\n0\n", "Duplicate endpoint");
        rejects("2 1 1 1\n0 localhost 1\n1 localhost 2\n0\n1\n", "intersections");
        rejects(std::string(100001, '#'), "100 KB");
        std::cout << "PASS: configuration tests\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
