#ifndef NETWORK_HPP
#define NETWORK_HPP

#include "config.hpp"
#include "message.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Network {
public:
    using ReceiveCallback = std::function<void(const Message&)>;
    using ErrorCallback = std::function<void(uint32_t, const std::string&)>;

    Network(const Configuration& config, uint32_t nodeId, const std::string& runId,
            std::chrono::milliseconds ioTimeout = std::chrono::seconds(5));
    ~Network();
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;

    void establishConnections(std::chrono::milliseconds timeout = std::chrono::seconds(15));
    // Callbacks run on receiver threads; the service will only enqueue events here.
    // Error callbacks must not throw. Callback targets must outlive stop().
    void startReceiving(ReceiveCallback onMessage, ErrorCallback onError);
    void sendMessage(uint32_t peerId, const Message& message);
    // Owner calls stop after producers stop. Never call it from a callback.
    void stop();

private:
    using Deadline = std::chrono::steady_clock::time_point;
    struct Peer {
        int fd = -1;
        std::mutex writeMutex;
    };

    Configuration config_;
    uint32_t nodeId_;
    std::string greeting_;
    std::chrono::milliseconds ioTimeout_;
    std::map<uint32_t, Peer> peers_;
    int listenFd_ = -1;
    bool established_ = false;
    bool receiversStarted_ = false;
    bool setupAttempted_ = false;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> setupCancelled_{false};
    std::vector<std::thread> receivers_;

    void openListener();
    void acceptPeers(Deadline deadline);
    void connectPeers(Deadline deadline);
    void writeGreeting(int fd, Deadline deadline);
    uint32_t readGreeting(int fd, Deadline deadline);
    void receiveLoop(uint32_t peerId, ReceiveCallback onMessage, ErrorCallback onError);
    void waitFor(int fd, short events, Deadline deadline);
    void readExact(int fd, uint8_t* data, size_t size, Deadline deadline);
    void writeExact(int fd, const uint8_t* data, size_t size, Deadline deadline);
};

#endif
