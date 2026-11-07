#include "config.hpp"
#include "network.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>

struct Event {
    Message message{};
    uint32_t peerId = 0;
    std::string error;
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 7) throw std::runtime_error("Expected config, node, run, startup-ms, io-ms, count");
        const auto config = parseConfigurationFile(argv[1]);
        const uint32_t nodeId = parseUnsigned(argv[2], "Node ID");
        const auto startup = std::chrono::milliseconds(parseUnsigned(argv[4], "Startup timeout"));
        const auto io = std::chrono::milliseconds(parseUnsigned(argv[5], "I/O timeout"));
        const uint32_t count = parseUnsigned(argv[6], "Message count");
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<Event> events;
        Network network(config, nodeId, argv[3], io);
        network.establishConnections(startup);
        network.startReceiving([&](const Message& message) {
            std::lock_guard<std::mutex> lock(mutex);
            events.push_back({message, message.senderId, {}});
            cv.notify_one();
        }, [&](uint32_t peerId, const std::string& error) {
            std::lock_guard<std::mutex> lock(mutex);
            events.push_back({{}, peerId, error});
            cv.notify_one();
        });
        for (const auto& node : config.nodes) {
            if (node.nodeId == nodeId) continue;
            for (uint64_t sequence = 0; sequence < count; ++sequence) {
                network.sendMessage(node.nodeId, {MessageType::Ping, nodeId, nodeId, sequence});
            }
            network.sendMessage(node.nodeId, {MessageType::Done, nodeId, nodeId, count});
        }

        std::map<uint32_t, uint64_t> received;
        std::set<uint32_t> finished;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (finished.size() + 1 < config.nodes.size()) {
            Event event;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (!cv.wait_until(lock, deadline, [&] { return !events.empty(); })) {
                    throw std::runtime_error("Test completion timed out");
                }
                event = events.front();
                events.pop_front();
            }
            if (!event.error.empty()) {
                // This diagnostic sends no more data after DONE on a peer's FIFO stream.
                if (finished.count(event.peerId)) continue;
                throw std::runtime_error(event.error);
            }
            const auto& message = event.message;
            if (finished.count(message.senderId) || message.requesterId != message.senderId ||
                    message.sequence != received[message.senderId]) {
                throw std::runtime_error("FIFO or identity check failed");
            }
            if (message.type == MessageType::Ping) ++received[message.senderId];
            else if (message.type == MessageType::Done && received[message.senderId] == count) {
                finished.insert(message.senderId);
            } else throw std::runtime_error("Wrong completion count");
        }
        network.stop();
        std::cout << "PASS: node " << nodeId << " received " << count << " ordered messages from each peer\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
