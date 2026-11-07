#include "mutex_service.hpp"
#include "network.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[]) {
    try {
        if (argc != 5) throw std::runtime_error("Expected config, node, run, request count");
        const auto config = parseConfigurationFile(argv[1]);
        const auto nodeId = parseUnsigned(argv[2], "Node ID");
        const auto count = parseUnsigned(argv[4], "Request count");
        // the controller ends the test only after every application has completed its workload.
        std::atomic<bool> completed{false};
        Network network(config, nodeId, argv[3], std::chrono::seconds(1));
        MutexService service(config, nodeId, [&](uint32_t peer, const Message& message) { network.sendMessage(peer, message); });
        try {
            network.establishConnections(std::chrono::seconds(3));
            service.start();
            network.startReceiving([&](const Message& message) { service.onMessage(message); },
                [&](uint32_t peer, const std::string& error) {
                    if (!completed.load()) service.onNetworkError(peer, error);
                });
            // hold each grant briefly so other application threads have time to contend.
            for (uint32_t i = 0; i < count; ++i) {
                service.cs_enter();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                service.cs_leave();
            }
            completed.store(true);
            std::cout << "COMPLETE " << nodeId << ' ' << count << std::endl;
            // the test controller keeps finished nodes voting until every application finishes.
            std::string command;
            if (!std::getline(std::cin, command) || command != "stop") throw std::runtime_error("Missing test shutdown command");
            service.checkHealth();
            service.stop();
            network.stop();
        } catch (...) {
            service.stop();
            network.stop();
            throw;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
