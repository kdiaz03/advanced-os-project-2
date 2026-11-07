#include "observer.hpp"

#include <stdexcept>
#include <utility>

Observer::Observer(const Configuration& config, uint32_t nodeId, SendCallback send, std::ostream& log,
                   std::chrono::milliseconds timeout)
    : config_(config), nodeId_(nodeId), observerId_(config.nodes.at(0).nodeId),
      send_(std::move(send)), log_(log), timeout_(timeout) {
    findNode(config_, nodeId_);
    if (!send_ || timeout_.count() <= 0) throw std::runtime_error("Invalid observer setup");
    if (nodeId_ == observerId_) {
        for (const auto& node : config_.nodes) counts_.emplace(node.nodeId, Counts{});
    }
}

void Observer::sendTo(uint32_t peer, const Message& message) {
    if (peer == nodeId_) onMessage(message);
    else send_(peer, message);
}

void Observer::exchange(MessageType type, uint64_t request) {
    const Message message{type, nodeId_, nodeId_, request};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!error_.empty()) throw std::runtime_error(error_);
        if (pending_ || request != completed_ + 1 || request > config_.params.requestsPerNode ||
                inside_ != (type == MessageType::ObserveExit)) throw std::runtime_error("Invalid observer call order");
        pending_ = message;
        acknowledged_ = false;
    }
    sendTo(observerId_, message);
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, timeout_, [&] { return acknowledged_ || !error_.empty(); })) {
        error_ = "Observer acknowledgment timed out";
    }
    if (!error_.empty()) throw std::runtime_error(error_);
    pending_.reset();
    inside_ = type == MessageType::ObserveEnter;
    if (!inside_) ++completed_;
}

void Observer::enter(uint64_t request) { exchange(MessageType::ObserveEnter, request); }
void Observer::leave(uint64_t request) { exchange(MessageType::ObserveExit, request); }

void Observer::fail(const std::string& reason) {
    if (failure_.empty()) failure_ = reason;
    log_ << "CHECK FAIL " << reason << '\n';
}

void Observer::record(const Message& message) {
    auto& count = counts_.at(message.senderId);
    const std::string identity = "node=" + std::to_string(message.senderId) + " request=" + std::to_string(message.sequence);
    if (message.type == MessageType::ObserveEnter) {
        if (message.sequence != count.entries + 1 || count.entries != count.exits ||
                message.sequence > config_.params.requestsPerNode) {
            fail("invalid entry " + identity);
            return;
        }
        if (!active_.empty()) {
            const auto holder = *active_.begin();
            fail("overlap " + identity + " while node=" + std::to_string(holder.first) +
                 " request=" + std::to_string(holder.second) + " is active");
        }
        active_[message.senderId] = message.sequence;
        ++count.entries;
        log_ << "CHECK ENTER " << identity << " active=" << active_.size() << '\n';
    } else {
        const auto active = active_.find(message.senderId);
        if (active == active_.end() || active->second != message.sequence || message.sequence != count.exits + 1) {
            fail("unmatched exit " + identity);
            return;
        }
        active_.erase(active);
        ++count.exits;
        log_ << "CHECK EXIT " << identity << " active=" << active_.size() << '\n';
    }
}

void Observer::onMessage(const Message& message) {
    findNode(config_, message.senderId);
    if (message.type == MessageType::EnterAck || message.type == MessageType::ExitAck) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto expected = pending_ && pending_->type == MessageType::ObserveEnter ? MessageType::EnterAck : MessageType::ExitAck;
        if (message.senderId != observerId_ || message.requesterId != nodeId_ || !pending_ ||
                message.sequence != pending_->sequence || message.type != expected || acknowledged_) {
            throw std::runtime_error("Unexpected observer acknowledgment");
        }
        acknowledged_ = true;
        cv_.notify_all();
        return;
    }
    if (nodeId_ != observerId_ || message.requesterId != message.senderId ||
            (message.type != MessageType::ObserveEnter && message.type != MessageType::ObserveExit)) {
        throw std::runtime_error("Invalid observer event");
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        record(message);
    }
    // acknowledge even an overlap; the observer must never queue entry for permission.
    const auto ack = message.type == MessageType::ObserveEnter ? MessageType::EnterAck : MessageType::ExitAck;
    sendTo(message.senderId, {ack, nodeId_, message.senderId, message.sequence});
}

void Observer::onNetworkError(uint32_t peerId, const std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (error_.empty()) error_ = "Peer " + std::to_string(peerId) + ": " + error;
    cv_.notify_all();
}

bool Observer::finish() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (nodeId_ != observerId_) throw std::runtime_error("Only the observer can finish the check");
    uint64_t entries = 0;
    uint64_t exits = 0;
    for (const auto& entry : counts_) {
        entries += entry.second.entries;
        exits += entry.second.exits;
        if (entry.second.entries != config_.params.requestsPerNode || entry.second.exits != config_.params.requestsPerNode) {
            fail("incomplete count node=" + std::to_string(entry.first));
        }
    }
    if (!active_.empty()) fail("critical section still active");
    if (!error_.empty()) fail(error_);
    const bool passed = failure_.empty();
    const uint64_t expected = static_cast<uint64_t>(config_.nodes.size()) * config_.params.requestsPerNode;
    log_ << "CHECK " << (passed ? "PASS" : "FAIL") << " entries=" << entries << " exits=" << exits
         << " expected=" << expected << '\n';
    return passed;
}
