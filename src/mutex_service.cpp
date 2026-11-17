#include "mutex_service.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

MutexService::MutexService(const Configuration& config, uint32_t nodeId, SendCallback send, ControlCallback control)
    : config_(config), nodeId_(nodeId), send_(std::move(send)), control_(std::move(control)),
      requester_(nodeId, findNode(config, nodeId).quorum), voter_(nodeId) {
    if (!send_) throw std::runtime_error("Missing send callback");
}

MutexService::~MutexService() { stop(); }

void MutexService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_ || stopping_) throw std::runtime_error("Service may only start once");
    worker_ = std::thread(&MutexService::workerLoop, this);
    started_ = true;
}

// call with the queue mutex held when inspecting shared run status.
void MutexService::requireRunning() const {
    if (!error_.empty()) throw std::runtime_error(error_);
    if (!started_ || stopping_) throw std::runtime_error("Mutex service is not running");
}

void MutexService::cs_enter() {
    std::unique_lock<std::mutex> lock(mutex_);
    requireRunning();
    if (callState_ != CallState::idle) throw std::runtime_error("Only one request may be outstanding");
    callState_ = CallState::waiting;
    events_.push_back({EventType::enter, {}});
    cv_.notify_all();
    // waiting releases the mutex, allowing receivers and the protocol worker to progress.
    cv_.wait(lock, [&] { return callState_ == CallState::granted || stopping_ || !error_.empty(); });
    requireRunning();
}

void MutexService::cs_leave() {
    std::unique_lock<std::mutex> lock(mutex_);
    requireRunning();
    if (callState_ != CallState::granted) throw std::runtime_error("Cannot leave outside the critical section");
    callState_ = CallState::leaving;
    events_.push_back({EventType::leave, {}});
    cv_.notify_all();
    // return after releases are dispatched, not after every remote voter acknowledges them.
    cv_.wait(lock, [&] { return callState_ == CallState::idle || stopping_ || !error_.empty(); });
    requireRunning();
}

// receiver threads enqueue messages instead of modifying requester or voter state.
void MutexService::onMessage(const Message& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || !error_.empty()) return;
    events_.push_back({EventType::message, message});
    cv_.notify_all();
}

// keep the original error and wake any application call waiting for progress.
void MutexService::fail(const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (error_.empty()) error_ = error.empty() ? "Mutex service failed" : error;
    cv_.notify_all();
}

void MutexService::onNetworkError(uint32_t peerId, const std::string& error) {
    fail("Peer " + std::to_string(peerId) + ": " + error);
}

void MutexService::checkHealth() {
    std::lock_guard<std::mutex> lock(mutex_);
    requireRunning();
}

// the inter-request deadline uses the grant time, not the later time the caller wakes.
std::chrono::steady_clock::time_point MutexService::grantTime() {
    std::lock_guard<std::mutex> lock(mutex_);
    requireRunning();
    if (callState_ != CallState::granted) throw std::runtime_error("No current grant time");
    return grantTime_;
}

// queue control messages behind earlier work so releases precede completion markers.
void MutexService::broadcastControl(MessageType type, uint64_t completed) {
    if (!control_ || (type != MessageType::Ready && type != MessageType::Done && type != MessageType::Goodbye)) {
        throw std::runtime_error("Invalid run control message");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    requireRunning();
    events_.push_back({EventType::broadcast, {type, nodeId_, nodeId_, completed}});
    cv_.notify_all();
}

void MutexService::dispatch(const std::vector<OutgoingMessage>& outgoing) {
    for (const auto& action : outgoing) {
        // only request through release are mutex protocol messages; control markers are not counted.
        const auto type = static_cast<uint32_t>(action.message.type);
        const bool protocol = type >= static_cast<uint32_t>(MessageType::Request) &&
                              type <= static_cast<uint32_t>(MessageType::Release);
        // self-voting uses the same queue and handlers without recursive protocol calls.
        if (action.peerId == nodeId_) {
            if (protocol) ++selfMessages_;
            onMessage(action.message);
        } else {
            if (protocol) ++networkMessages_;
            send_(action.peerId, action.message);
        }
    }
}

std::vector<OutgoingMessage> MutexService::receive(const Message& message) {
    const auto& sender = findNode(config_, message.senderId);
    if (message.type == MessageType::Ready || message.type == MessageType::Done || message.type == MessageType::Goodbye) {
        if (!control_) throw std::runtime_error("Missing run control handler");
        control_(message);
        return {};
    }
    if (message.sequence == 0) throw std::runtime_error("Request sequence must be positive");
    // the next local request must exceed every request sequence processed so far.
    sequence_ = std::max(sequence_, message.sequence);
    switch (message.type) {
        case MessageType::Request:
        case MessageType::Relinquish:
        case MessageType::Release:
            // check reverse membership: this requester must actually use us as a voter.
            if (std::find(sender.quorum.begin(), sender.quorum.end(), nodeId_) == sender.quorum.end()) {
                throw std::runtime_error("Requester does not use this voter");
            }
            return voter_.handle(message);
        case MessageType::Locked:
        case MessageType::Failed:
        case MessageType::Inquire:
            return requester_.handle(message);
        default:
            throw std::runtime_error("Unexpected message at mutex service");
    }
}

void MutexService::process(const Event& event) {
    switch (event.type) {
        case EventType::enter:
            if (sequence_ == std::numeric_limits<uint64_t>::max()) throw std::runtime_error("Request sequence exhausted");
            dispatch(requester_.enter(++sequence_));
            break;
        case EventType::leave:
            dispatch(requester_.leave());
            break;
        case EventType::message:
            dispatch(receive(event.message));
            break;
        // final shutdown is allowed only after both local requests and local votes are idle.
        case EventType::broadcast: {
            if (!requester_.idle() || (event.message.type == MessageType::Goodbye && !voter_.idle())) {
                throw std::runtime_error("Run control attempted before votes were released");
            }
            std::vector<OutgoingMessage> outgoing;
            for (const auto& node : config_.nodes) outgoing.push_back({node.nodeId, event.message});
            dispatch(outgoing);
            break;
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.type == EventType::leave) callState_ = CallState::idle;
    else if (requester_.inCs() && callState_ == CallState::waiting) {
        grantTime_ = std::chrono::steady_clock::now();
        callState_ = CallState::granted;
    }
    cv_.notify_all();
}

void MutexService::workerLoop() {
    try {
        while (true) {
            Event event;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] { return !events_.empty() || stopping_ || !error_.empty(); });
                if (stopping_ || !error_.empty()) return;
                event = events_.front();
                events_.pop_front();
            }
            // process outside the queue lock so socket writes do not block event producers.
            process(event);
        }
    } catch (const std::exception& error) {
        fail(error.what());
    }
}

void MutexService::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        cv_.notify_all();
    }
    if (worker_.joinable()) worker_.join();
}
