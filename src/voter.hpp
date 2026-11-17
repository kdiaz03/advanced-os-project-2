#ifndef VOTER_HPP
#define VOTER_HPP

#include "message.hpp"

#include <map>
#include <optional>
#include <set>
#include <vector>

// vote-side state; transitions return messages for the worker to dispatch.
class Voter {
public:
    explicit Voter(uint32_t nodeId) : nodeId_(nodeId) {}
    std::vector<OutgoingMessage> handle(const Message& message);
    bool idle() const { return !holder_ && waiting_.empty(); }

private:
    uint32_t nodeId_;
    std::optional<RequestId> holder_;
    // the ordered set keeps the highest-priority waiting request first.
    std::set<RequestId> waiting_;
    // waiting requests whose requester already knows it cannot win this vote yet.
    std::set<RequestId> failed_;
    // remember completed requests too, so delayed duplicates remain stale.
    std::map<uint32_t, uint64_t> latestSequence_;
    bool inquiryPending_ = false;

    OutgoingMessage reply(MessageType type, RequestId request) const;
    std::vector<OutgoingMessage> onRequest(RequestId request);
    std::vector<OutgoingMessage> grantNext();
};

#endif
