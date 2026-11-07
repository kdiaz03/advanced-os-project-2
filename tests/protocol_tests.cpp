#include "requester.hpp"
#include "voter.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Message reply(MessageType type, uint32_t voter, uint64_t sequence = 5) {
    return {type, voter, 0, sequence};
}

void requesterCases() {
    Requester requester(0, {1, 2, 3});
    require(requester.enter(5).size() == 3, "Missing requests");
    requester.handle(reply(MessageType::Locked, 1));
    requester.handle(reply(MessageType::Locked, 2));
    require(requester.handle(reply(MessageType::Inquire, 1)).empty(), "Inquiry should be deferred");
    auto result = requester.handle(reply(MessageType::Failed, 3));
    require(result.size() == 1 && result[0].peerId == 1 &&
        result[0].message.type == MessageType::Relinquish, "Deferred inquiry was not revisited");
    requester.handle(reply(MessageType::Locked, 3));
    require(!requester.inCs(), "Returned vote was still counted");
    result = requester.handle(reply(MessageType::Inquire, 2));
    require(result.size() == 1 && result[0].peerId == 2, "Returned vote must keep preemption possible");
    requester.handle(reply(MessageType::Locked, 1));
    require(!requester.inCs(), "Second returned vote was still counted");
    requester.handle(reply(MessageType::Locked, 2));
    require(requester.inCs(), "All votes should grant entry");
    require(requester.handle(reply(MessageType::Inquire, 1)).empty(), "Relinquished inside CS");
    require(requester.handle(reply(MessageType::Failed, 3)).empty(), "Changed votes inside CS");
    const auto releases = requester.leave();
    require(releases.size() == 3 && !requester.inCs(), "Missing releases");
    for (const auto& release : releases) {
        require(release.message.type == MessageType::Release && release.message.sequence == 5, "Wrong release identity");
    }
    requester.enter(6);
    for (uint32_t voter : {1, 2, 3}) requester.handle(reply(MessageType::Locked, voter));
    require(!requester.inCs(), "Old votes granted a new request");
    requester.handle(reply(MessageType::Failed, 3, 6));
    requester.handle(reply(MessageType::Locked, 3, 6));
    requester.handle(reply(MessageType::Locked, 1, 6));
    require(requester.handle(reply(MessageType::Inquire, 1, 6)).empty(), "Resolved failure was retained");
    requester.handle(reply(MessageType::Locked, 1, 6));
    require(!requester.inCs(), "Duplicate grant was counted twice");
    requester.handle(reply(MessageType::Locked, 2, 6));
    require(requester.inCs(), "Deferred inquiry prevented full-quorum entry");
    requester.leave();
    require(requester.handle(reply(MessageType::Inquire, 1, 6)).empty(), "Late inquiry changed idle state");

    Requester failedFirst(0, {1, 2, 3});
    failedFirst.enter(5);
    failedFirst.handle(reply(MessageType::Failed, 3));
    failedFirst.handle(reply(MessageType::Locked, 1));
    require(failedFirst.handle(reply(MessageType::Inquire, 1)).size() == 1, "Failure-before-inquiry did not yield");
}

struct Node {
    Requester requester;
    Voter voter;
    uint64_t sequence = 0;
    unsigned completed = 0;
    Node(uint32_t id, const std::vector<uint32_t>& quorum) : requester(id, quorum), voter(id) {}
};

struct Simulation {
    std::map<uint32_t, Node> nodes;
    std::vector<OutgoingMessage> pending;
    std::vector<uint32_t> entries;

    explicit Simulation(const std::vector<std::vector<uint32_t>>& quorums, uint32_t firstId = 0) {
        for (size_t i = 0; i < quorums.size(); ++i) {
            const auto id = firstId + static_cast<uint32_t>(i);
            nodes.try_emplace(id, id, quorums[i]);
        }
    }

    void append(const std::vector<OutgoingMessage>& actions) {
        pending.insert(pending.end(), actions.begin(), actions.end());
    }

    void enter(uint32_t id) { auto& node = nodes.at(id); append(node.requester.enter(++node.sequence)); }
    void leave(uint32_t id) { auto& node = nodes.at(id); append(node.requester.leave()); ++node.completed; }

    bool eligible(size_t index) const {
        for (size_t i = 0; i < index; ++i) {
            if (pending[i].peerId == pending[index].peerId &&
                    pending[i].message.senderId == pending[index].message.senderId) return false;
        }
        return true;
    }

    void deliver(size_t index) {
        require(eligible(index), "Test would violate per-peer FIFO");
        const auto action = pending.at(index);
        pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(index));
        auto& node = nodes.at(action.peerId);
        const bool wasInCs = node.requester.inCs();
        node.sequence = std::max(node.sequence, action.message.sequence);
        const auto type = action.message.type;
        if (type == MessageType::Request || type == MessageType::Release || type == MessageType::Relinquish) {
            append(node.voter.handle(action.message));
        } else append(node.requester.handle(action.message));
        if (!wasInCs && node.requester.inCs()) entries.push_back(action.peerId);
        size_t active = 0;
        for (const auto& entry : nodes) if (entry.second.requester.inCs()) ++active;
        require(active <= 1, "Overlapping critical sections in simulation");
    }

    void deliver(uint32_t from, uint32_t to, MessageType type) {
        for (size_t i = 0; i < pending.size(); ++i) {
            if (pending[i].peerId == to && pending[i].message.senderId == from && pending[i].message.type == type) {
                deliver(i);
                return;
            }
        }
        throw std::runtime_error("Expected message is missing");
    }

    void grant(uint32_t requester, uint32_t voter) {
        deliver(requester, voter, MessageType::Request);
        deliver(voter, requester, MessageType::Locked);
    }
};

void paperCycle(bool failureFirst) {
    Simulation sim({{1,2,3,4}, {2,5,8,11}, {3,6,8,13}, {4,6,10,11}, {1,5,6,7},
        {2,6,9,12}, {2,7,10,13}, {1,8,9,10}, {3,7,9,11}, {3,5,10,12},
        {1,11,12,13}, {4,7,8,12}, {4,5,9,13}}, 1);
    for (uint32_t id : {11, 7, 8}) sim.enter(id);
    for (uint32_t voter : {11, 12, 13}) sim.grant(11, voter);
    for (uint32_t voter : {7, 2, 10}) sim.grant(7, voter);
    for (uint32_t voter : {8, 1, 9}) sim.grant(8, voter);
    sim.deliver(8, 10, MessageType::Request);
    sim.deliver(10, 8, MessageType::Failed);
    sim.deliver(11, 1, MessageType::Request);
    sim.deliver(7, 13, MessageType::Request);
    if (failureFirst) {
        sim.deliver(1, 11, MessageType::Failed);
        sim.deliver(13, 11, MessageType::Inquire);
    } else {
        sim.deliver(13, 11, MessageType::Inquire);
        require(sim.entries.empty(), "Cycle should still be blocked");
        sim.deliver(1, 11, MessageType::Failed);
    }
    sim.deliver(11, 13, MessageType::Relinquish);
    sim.deliver(13, 7, MessageType::Locked);
    require(sim.nodes.at(7).requester.inCs(), "Node 7 did not break the cycle");
    for (size_t steps = 0; steps < 1000; ++steps) {
        for (auto& entry : sim.nodes) if (entry.second.requester.inCs()) sim.leave(entry.first);
        if (sim.pending.empty()) break;
        sim.deliver(0);
    }
    require(sim.pending.empty() && sim.entries == std::vector<uint32_t>({7, 8, 11}), "Wrong paper-cycle completion order");
}

void randomizedSchedules() {
    for (unsigned seed = 0; seed < 40; ++seed) {
        Simulation sim({{0,1}, {1,2}, {0,2}});
        std::mt19937 random(seed);
        for (uint32_t id : {0, 1, 2}) sim.enter(id);
        for (size_t steps = 0; steps < 100000; ++steps) {
            std::vector<size_t> choices;
            for (size_t i = 0; i < sim.pending.size(); ++i) if (sim.eligible(i)) choices.push_back(i);
            std::vector<uint32_t> active;
            for (const auto& entry : sim.nodes) if (entry.second.requester.inCs()) active.push_back(entry.first);
            if (choices.empty() && active.empty()) break;
            const size_t choice = random() % (choices.size() + active.size());
            if (choice < choices.size()) sim.deliver(choices[choice]);
            else {
                const uint32_t id = active[choice - choices.size()];
                sim.leave(id);
                if (sim.nodes.at(id).completed < 20) sim.enter(id);
            }
        }
        require(sim.pending.empty(), "Simulation did not drain");
        for (const auto& entry : sim.nodes) require(entry.second.completed == 20, "Request did not finish");
    }
}
}

int main() {
    try {
        requesterCases();
        paperCycle(false);
        paperCycle(true);
        randomizedSchedules();
        std::cout << "PASS: requester edge cases, paper cycle in both orders, 40 FIFO schedules (2400 CS entries)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
