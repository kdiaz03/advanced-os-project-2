#ifndef MUTEX_SERVICE_HPP
#define MUTEX_SERVICE_HPP

#include "config.hpp"
#include "requester.hpp"
#include "voter.hpp"

#include <condition_variable>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class MutexService {
public:
    using SendCallback = std::function<void(uint32_t, const Message&)>;
    using ControlCallback = std::function<void(const Message&)>;
    MutexService(const Configuration& config, uint32_t nodeId, SendCallback send, ControlCallback control = {});
    ~MutexService();
    MutexService(const MutexService&) = delete;
    MutexService& operator=(const MutexService&) = delete;

    void start();
    // one application thread calls enter/leave; receiver threads only enqueue.
    void cs_enter();
    void cs_leave();
    void onMessage(const Message& message);
    void onNetworkError(uint32_t peerId, const std::string& error);
    void checkHealth();
    std::chrono::steady_clock::time_point grantTime();
    void broadcastControl(MessageType type, uint64_t completed = 0);
    // stop aborts pending calls; normal distributed completion belongs to the application.
    void stop();

private:
    enum class EventType { enter, leave, message, broadcast };
    struct Event {
        EventType type;
        Message message{};
    };
    enum class CallState { idle, waiting, granted, leaving };

    Configuration config_;
    uint32_t nodeId_;
    SendCallback send_;
    ControlCallback control_;
    Requester requester_;
    Voter voter_;
    uint64_t sequence_ = 0;
    std::thread worker_;

    // this mutex guards the queue and call notifications, not protocol state.
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Event> events_;
    CallState callState_ = CallState::idle;
    bool started_ = false;
    bool stopping_ = false;
    std::string error_;
    std::chrono::steady_clock::time_point grantTime_{};

    void requireRunning() const;
    void fail(const std::string& error);
    void workerLoop();
    void process(const Event& event);
    std::vector<OutgoingMessage> receive(const Message& message);
    void dispatch(const std::vector<OutgoingMessage>& outgoing);
};

#endif
