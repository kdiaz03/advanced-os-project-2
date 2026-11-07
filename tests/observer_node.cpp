#include "network.hpp"
#include "observer.hpp"
#include "run_control.hpp"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>

int main(int argc, char* argv[]) {
    try {
        if (argc != 4) throw std::runtime_error("Expected config, node ID, run ID");
        const auto config = parseConfigurationFile(argv[1]);
        const uint32_t nodeId = parseUnsigned(argv[2], "Node ID");
        if (config.nodes.size() != 2 || config.params.requestsPerNode != 1) {
            throw std::runtime_error("Negative test needs two nodes and one request each");
        }
        const uint32_t first = config.nodes.front().nodeId;
        const uint32_t peer = nodeId == first ? config.nodes.back().nodeId : first;
        std::mutex mutex;
        std::condition_variable cv;
        bool begin = false;
        bool peerDone = false;
        std::string error;
        RunControl control(config);
        Network network(config, nodeId, argv[3], std::chrono::seconds(1));
        Observer observer(config, nodeId,
            [&](uint32_t target, const Message& message) { network.sendMessage(target, message); }, std::cout,
            std::chrono::seconds(2));
        MutexService service(config, nodeId,
            [&](uint32_t target, const Message& message) { network.sendMessage(target, message); },
            [&](const Message& message) { control.onMessage(message); });
        const auto waitFor = [&](bool& ready) {
            std::unique_lock<std::mutex> lock(mutex);
            if (!cv.wait_for(lock, std::chrono::seconds(3), [&] { return ready || !error.empty(); })) {
                throw std::runtime_error("Negative test coordination timed out");
            }
            if (!error.empty()) throw std::runtime_error(error);
        };
        bool passed = true;
        try {
            network.establishConnections(std::chrono::seconds(3));
            service.start();
            network.startReceiving([&](const Message& message) {
                if (isObserverMessage(message.type)) observer.onMessage(message);
                else if (message.type == MessageType::Ping) {
                    std::lock_guard<std::mutex> lock(mutex);
                    begin = true;
                    cv.notify_one();
                } else {
                    service.onMessage(message);
                    if (message.type == MessageType::Done) {
                        std::lock_guard<std::mutex> lock(mutex);
                        peerDone = true;
                        cv.notify_one();
                    }
                }
            }, [&](uint32_t sender, const std::string& reason) {
                observer.onNetworkError(sender, reason);
                service.onNetworkError(sender, reason);
                std::lock_guard<std::mutex> lock(mutex);
                error = reason;
                cv.notify_one();
            });
            service.broadcastControl(MessageType::Ready);
            control.waitFor(MessageType::Ready, service);

            // bypass the mutex only here; the first interval stays open until the second finishes.
            if (nodeId == first) {
                observer.enter(1);
                network.sendMessage(peer, {MessageType::Ping, nodeId, nodeId, 0});
                waitFor(peerDone);
                observer.leave(1);
            } else {
                waitFor(begin);
                observer.enter(1);
                observer.leave(1);
            }
            service.broadcastControl(MessageType::Done, 1);
            control.waitFor(MessageType::Done, service);
            if (nodeId == first) passed = observer.finish();
            service.broadcastControl(MessageType::Goodbye);
            control.waitFor(MessageType::Goodbye, service);
            service.stop();
            network.stop();
        } catch (...) {
            service.stop();
            network.stop();
            throw;
        }
        return passed ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Test error: " << error.what() << '\n';
        return 2;
    }
}
