#include "run_control.hpp"

#include <stdexcept>

void RunControl::onMessage(const Message& message) {
    findNode(config_, message.senderId);
    if (message.requesterId != message.senderId) throw std::runtime_error("Invalid run control sender");
    const uint64_t expected = message.type == MessageType::Done ? config_.params.requestsPerNode : 0;
    if (message.sequence != expected) throw std::runtime_error("Wrong run completion count");
    std::lock_guard<std::mutex> lock(mutex_);
    if (message.type == MessageType::Done && !received_[MessageType::Ready].count(message.senderId)) {
        throw std::runtime_error("Completion before startup");
    }
    if (message.type == MessageType::Goodbye && !received_[MessageType::Done].count(message.senderId)) {
        throw std::runtime_error("Shutdown before completion");
    }
    if (message.type != MessageType::Ready && message.type != MessageType::Done && message.type != MessageType::Goodbye) {
        throw std::runtime_error("Unexpected run control message");
    }
    if (!received_[message.type].insert(message.senderId).second) throw std::runtime_error("Duplicate run control message");
    cv_.notify_all();
}

void RunControl::waitFor(MessageType type, MutexService& service) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (received_[type].size() != config_.nodes.size()) {
        cv_.wait_for(lock, std::chrono::milliseconds(100));
        lock.unlock();
        service.checkHealth();
        lock.lock();
    }
    lock.unlock();
    service.checkHealth();
}
