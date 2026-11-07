#include "voter.hpp"

#include <stdexcept>

OutgoingMessage Voter::reply(MessageType type, RequestId request) const {
    return {request.nodeId, {type, nodeId_, request.nodeId, request.sequence}};
}

std::vector<OutgoingMessage> Voter::onRequest(RequestId request) {
    // do not resurrect a completed request or enqueue a duplicate request.
    if (request.sequence <= latestSequence_[request.nodeId]) return {};
    if (holder_ && holder_->nodeId == request.nodeId) {
        throw std::runtime_error("Requester already holds this vote");
    }
    for (const auto& pending : waiting_) {
        if (pending.nodeId == request.nodeId) throw std::runtime_error("Requester already queued");
    }
    latestSequence_[request.nodeId] = request.sequence;
    // a free voter may grant immediately, but can have only one holder.
    if (!holder_) {
        holder_ = request;
        return {reply(MessageType::Locked, request)};
    }

    waiting_.insert(request);
    // an earlier queued request also makes the new requester fail.
    if (*holder_ < request || *waiting_.begin() < request) {
        return {reply(MessageType::Failed, request)};
    }
    // wait for the outstanding inquiry's release or relinquishment before sending another.
    if (inquiryPending_) return {};
    inquiryPending_ = true;
    return {reply(MessageType::Inquire, *holder_)};
}

// reset inquiry state because the next vote belongs to a different request.
std::vector<OutgoingMessage> Voter::grantNext() {
    holder_.reset();
    inquiryPending_ = false;
    if (waiting_.empty()) return {};
    holder_ = *waiting_.begin();
    waiting_.erase(waiting_.begin());
    return {reply(MessageType::Locked, *holder_)};
}

std::vector<OutgoingMessage> Voter::handle(const Message& message) {
    if (message.senderId != message.requesterId || message.sequence == 0) {
        throw std::runtime_error("Invalid voter request identity");
    }
    const RequestId request{message.sequence, message.requesterId};
    switch (message.type) {
        case MessageType::Request:
            return onRequest(request);
        case MessageType::Relinquish:
            if (!holder_ || !(*holder_ == request) || !inquiryPending_) return {};
            // choose the best request now, not necessarily the one that triggered inquiry.
            waiting_.insert(*holder_);
            return grantNext();
        // a late release must not remove the vote granted to a newer request.
        case MessageType::Release:
            if (!holder_ || !(*holder_ == request)) return {};
            return grantNext();
        default:
            throw std::runtime_error("Unexpected message at voter");
    }
}
