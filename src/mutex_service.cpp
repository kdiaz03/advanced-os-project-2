#include "mutex_service.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

MutexService::MutexService(const Configuration& config, uint32_t nodeId, SendCallback send)
    : config_(config), nodeId_(nodeId), send_(std::move(send)),
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
    cv_.wait(lock, [&] { return callState_ == CallState::idle || stopping_ || !error_.empty(); });
    requireRunning();
}

void MutexService::onMessage(const Message& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || !error_.empty()) return;
    events_.push_back({EventType::message, message});
    cv_.notify_all();
}

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

void MutexService::dispatch(const std::vector<OutgoingMessage>& outgoing) {
    for (const auto& action : outgoing) {
        if (action.peerId == nodeId_) onMessage(action.message);
        else send_(action.peerId, action.message);
    }
}

std::vector<OutgoingMessage> MutexService::receive(const Message& message) {
    const auto& sender = findNode(config_, message.senderId);
    if (message.sequence == 0) throw std::runtime_error("Request sequence must be positive");
    sequence_ = std::max(sequence_, message.sequence);
    switch (message.type) {
        case MessageType::Request:
        case MessageType::Relinquish:
        case MessageType::Release:
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
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (event.type == EventType::leave) callState_ = CallState::idle;
    else if (requester_.inCs() && callState_ == CallState::waiting) callState_ = CallState::granted;
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
