#ifndef OBSERVER_HPP
#define OBSERVER_HPP

#include "config.hpp"
#include "message.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>

class Observer {
public:
    using SendCallback = std::function<void(uint32_t, const Message&)>;
    Observer(const Configuration& config, uint32_t nodeId, SendCallback send, std::ostream& log,
             std::chrono::milliseconds timeout = std::chrono::seconds(5));
    // one application thread brackets its cs body with these acknowledged calls.
    void enter(uint64_t request);
    void leave(uint64_t request);
    void onMessage(const Message& message);
    void onNetworkError(uint32_t peerId, const std::string& error);
    // the observer node checks final counts after every node has reported done.
    bool finish();
    uint32_t nodeId() const { return observerId_; }

private:
    struct Counts { uint64_t entries = 0; uint64_t exits = 0; };
    Configuration config_;
    uint32_t nodeId_;
    uint32_t observerId_;
    SendCallback send_;
    std::ostream& log_;
    std::chrono::milliseconds timeout_;
    std::mutex mutex_;
    std::condition_variable cv_;

    // client acknowledgment state, shared with network receiver threads.
    std::optional<Message> pending_;
    bool acknowledged_ = false;
    bool inside_ = false;
    uint64_t completed_ = 0;
    std::string error_;

    // only the observer node uses these records; failures remain latched until shutdown.
    std::map<uint32_t, Counts> counts_;
    std::map<uint32_t, uint64_t> active_;
    std::string failure_;

    void sendTo(uint32_t peer, const Message& message);
    void exchange(MessageType type, uint64_t request);
    void record(const Message& message);
    void fail(const std::string& reason);
};

#endif
