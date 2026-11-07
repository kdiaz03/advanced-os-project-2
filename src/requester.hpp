#ifndef REQUESTER_HPP
#define REQUESTER_HPP

#include "message.hpp"

#include <set>
#include <vector>

class Requester {
public:
    Requester(uint32_t nodeId, const std::vector<uint32_t>& quorum);
    std::vector<OutgoingMessage> enter(uint64_t sequence);
    std::vector<OutgoingMessage> leave();
    std::vector<OutgoingMessage> handle(const Message& message);
    bool inCs() const { return phase_ == Phase::inCs; }
    bool idle() const { return phase_ == Phase::idle; }

private:
    enum class Phase { idle, requesting, inCs };
    uint32_t nodeId_;
    std::set<uint32_t> quorum_;
    uint64_t sequence_ = 0;
    Phase phase_ = Phase::idle;
    std::set<uint32_t> held_;
    std::set<uint32_t> unavailable_;
    std::set<uint32_t> inquiries_;

    OutgoingMessage sendTo(uint32_t voter, MessageType type) const;
    std::vector<OutgoingMessage> revisitInquiries();
};

#endif
