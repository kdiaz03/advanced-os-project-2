#include "requester.hpp"

#include <stdexcept>

Requester::Requester(uint32_t nodeId, const std::vector<uint32_t>& quorum)
    : nodeId_(nodeId), quorum_(quorum.begin(), quorum.end()) {
    if (quorum_.empty() || quorum_.size() != quorum.size()) throw std::runtime_error("Invalid requester quorum");
}

OutgoingMessage Requester::sendTo(uint32_t voter, MessageType type) const {
    return {voter, {type, nodeId_, nodeId_, sequence_}};
}

// start a fresh request only after the previous critical section has been released.
std::vector<OutgoingMessage> Requester::enter(uint64_t sequence) {
    if (phase_ != Phase::idle || sequence <= sequence_) throw std::runtime_error("Invalid new request");
    sequence_ = sequence;
    phase_ = Phase::requesting;
    // votes and deferred inquiries belong only to this request identity.
    held_.clear();
    unavailable_.clear();
    inquiries_.clear();
    std::vector<OutgoingMessage> outgoing;
    for (uint32_t voter : quorum_) outgoing.push_back(sendTo(voter, MessageType::Request));
    return outgoing;
}

// a granted request holds the entire quorum until the application leaves.
std::vector<OutgoingMessage> Requester::leave() {
    if (phase_ != Phase::inCs) throw std::runtime_error("Cannot leave outside the critical section");
    std::vector<OutgoingMessage> outgoing;
    for (uint32_t voter : quorum_) outgoing.push_back(sendTo(voter, MessageType::Release));
    phase_ = Phase::idle;
    held_.clear();
    unavailable_.clear();
    inquiries_.clear();
    return outgoing;
}

std::vector<OutgoingMessage> Requester::revisitInquiries() {
    std::vector<OutgoingMessage> outgoing;
    // an inquiry alone is not enough to return a vote; we need evidence of a missing vote.
    if (unavailable_.empty()) return outgoing;
    for (uint32_t voter : inquiries_) {
        if (held_.erase(voter)) {
            // a returned vote stays unavailable until that voter grants it again.
            unavailable_.insert(voter);
            outgoing.push_back(sendTo(voter, MessageType::Relinquish));
        }
    }
    inquiries_.clear();
    return outgoing;
}

std::vector<OutgoingMessage> Requester::handle(const Message& message) {
    if (message.requesterId != nodeId_ || !quorum_.count(message.senderId)) {
        throw std::runtime_error("Invalid requester reply identity");
    }
    // ignore old replies and retain all votes once the critical section has begun.
    if (phase_ != Phase::requesting || message.sequence != sequence_) return {};
    switch (message.type) {
        // sets avoid double-counting grants, and a new grant clears that voter's failure.
        case MessageType::Locked:
            held_.insert(message.senderId);
            unavailable_.erase(message.senderId);
            break;
        case MessageType::Failed:
            if (!held_.count(message.senderId)) unavailable_.insert(message.senderId);
            break;
        // remember the inquiry so a later failure can trigger relinquishment.
        case MessageType::Inquire:
            if (held_.count(message.senderId)) inquiries_.insert(message.senderId);
            break;
        default:
            throw std::runtime_error("Unexpected message at requester");
    }
    auto outgoing = revisitInquiries();
    // entry is decided in this transition, before an application thread can wake.
    if (held_ == quorum_) phase_ = Phase::inCs;
    return outgoing;
}
