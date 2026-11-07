#ifndef VOTER_HPP
#define VOTER_HPP

#include "message.hpp"

#include <map>
#include <optional>
#include <set>
#include <vector>

class Voter {
public:
    explicit Voter(uint32_t nodeId) : nodeId_(nodeId) {}
    std::vector<OutgoingMessage> handle(const Message& message);
    bool idle() const { return !holder_ && waiting_.empty(); }

private:
    uint32_t nodeId_;
    std::optional<RequestId> holder_;
    std::set<RequestId> waiting_;
    std::map<uint32_t, uint64_t> latestSequence_;
    bool inquiryPending_ = false;

    OutgoingMessage reply(MessageType type, RequestId request) const;
    std::vector<OutgoingMessage> onRequest(RequestId request);
    std::vector<OutgoingMessage> grantNext();
};

#endif
