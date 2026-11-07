#include "voter.hpp"

#include <iostream>
#include <stdexcept>

namespace {
Message request(uint32_t node, uint64_t sequence = 1, MessageType type = MessageType::Request) {
    return {type, node, node, sequence};
}

void expect(const std::vector<OutgoingMessage>& result, MessageType type, uint32_t node, uint64_t sequence = 1) {
    if (result.size() != 1 || result[0].peerId != node || result[0].message.type != type ||
            result[0].message.senderId != 13 || result[0].message.requesterId != node ||
            result[0].message.sequence != sequence) throw std::runtime_error("Wrong voter reply");
}

void silent(const std::vector<OutgoingMessage>& result) {
    if (!result.empty()) throw std::runtime_error("Unexpected voter reply");
}
}

int main() {
    try {
        // reproduce voter 13's queue, including a new request that overtakes the inquiry trigger.
        Voter voter(13);
        expect(voter.handle(request(11)), MessageType::Locked, 11);
        silent(voter.handle(request(11)));
        expect(voter.handle(request(7)), MessageType::Inquire, 11);
        expect(voter.handle(request(8)), MessageType::Failed, 8);
        silent(voter.handle(request(3)));
        silent(voter.handle(request(7, 1, MessageType::Release)));
        silent(voter.handle(request(11, 2, MessageType::Release)));
        expect(voter.handle(request(11, 1, MessageType::Relinquish)), MessageType::Locked, 3);
        silent(voter.handle(request(11, 1, MessageType::Relinquish)));
        expect(voter.handle(request(3, 1, MessageType::Release)), MessageType::Locked, 7);
        expect(voter.handle(request(7, 1, MessageType::Release)), MessageType::Locked, 8);
        expect(voter.handle(request(8, 1, MessageType::Release)), MessageType::Locked, 11);
        silent(voter.handle(request(11, 1, MessageType::Relinquish)));
        silent(voter.handle(request(11, 1, MessageType::Release)));
        silent(voter.handle(request(11)));
        // an old release must not clear this newer vote for the same node.
        expect(voter.handle(request(11, 2)), MessageType::Locked, 11, 2);
        silent(voter.handle(request(11, 1, MessageType::Release)));
        expect(voter.handle(request(1, 3)), MessageType::Failed, 1, 3);
        expect(voter.handle(request(11, 2, MessageType::Release)), MessageType::Locked, 1, 3);

        // a holder that finishes its cs releases normally instead of relinquishing.
        Voter releaseDuringInquiry(13);
        expect(releaseDuringInquiry.handle(request(11)), MessageType::Locked, 11);
        expect(releaseDuringInquiry.handle(request(7)), MessageType::Inquire, 11);
        expect(releaseDuringInquiry.handle(request(11, 1, MessageType::Release)), MessageType::Locked, 7);
        silent(releaseDuringInquiry.handle(request(11, 1, MessageType::Relinquish)));
        bool rejected = false;
        try { releaseDuringInquiry.handle(request(7, 2)); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Accepted concurrent requests from one node");
        std::cout << "PASS: voter priority, inquiry, relinquishment, release, and stale identities\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
