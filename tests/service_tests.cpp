#include "mutex_service.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Configuration twoNodes() {
    return {{2, 1, 1, 1}, {{0, "localhost", 24000, {0, 1}}, {1, "localhost", 24001, {0, 1}}}};
}

void ready(MutexService& service, std::future<void>& call) {
    if (call.wait_for(2s) != std::future_status::ready) {
        service.stop();
        throw std::runtime_error("API call did not finish");
    }
}

void mustFail(std::future<void>& call, const std::string& text) {
    try { call.get(); }
    catch (const std::runtime_error& error) {
        require(std::string(error.what()).find(text) != std::string::npos, "Wrong API error: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("API failure was not reported");
}

struct Capture {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<Message> messages;
    void send(uint32_t peer, const Message& message) {
        require(peer == 1, "Self-message used network");
        std::lock_guard<std::mutex> lock(mutex);
        messages.push_back(message);
        cv.notify_one();
    }
    Message take(MessageType type) {
        std::unique_lock<std::mutex> lock(mutex);
        const auto find = [&] {
            return std::find_if(messages.begin(), messages.end(), [=](const Message& message) { return message.type == type; });
        };
        require(cv.wait_for(lock, 2s, [&] { return find() != messages.end(); }), "Expected service message missing");
        const auto match = find();
        const auto result = *match;
        messages.erase(match);
        return result;
    }
};

void blockingAndClock() {
    Capture capture;
    MutexService service(twoNodes(), 0, [&](uint32_t peer, const Message& message) { capture.send(peer, message); });
    service.start();
    auto call = std::async(std::launch::async, [&] { service.cs_enter(); });
    try {
        const auto first = capture.take(MessageType::Request);
        require(first.sequence == 1, "Wrong initial sequence");
        require(call.wait_for(0s) != std::future_status::ready, "Entered without remote vote");
        bool rejected = false;
        try { service.cs_enter(); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Allowed a second outstanding API call");
        service.onMessage({MessageType::Request, 1, 1, 100});
        require(capture.take(MessageType::Failed).sequence == 100, "Voter stopped while application blocked");
        service.onMessage({MessageType::Locked, 1, 0, first.sequence});
        ready(service, call);
        call.get();
        service.cs_leave();
        require(capture.take(MessageType::Release).sequence == first.sequence, "Wrong release sequence");
        require(capture.take(MessageType::Locked).requesterId == 1, "Waiting requester was not granted");
        service.onMessage({MessageType::Release, 1, 1, 100});
        call = std::async(std::launch::async, [&] { service.cs_enter(); });
        const auto second = capture.take(MessageType::Request);
        require(second.sequence == 101, "New sequence did not exceed observed request");
        service.onMessage({MessageType::Locked, 1, 0, first.sequence});
        service.onMessage({MessageType::Inquire, 1, 0, first.sequence});
        require(call.wait_for(0s) != std::future_status::ready, "Stale reply granted entry");
        service.onMessage({MessageType::Locked, 1, 0, second.sequence});
        ready(service, call);
        call.get();
        service.cs_leave();
        service.stop();
    } catch (...) {
        service.stop();
        throw;
    }
}

void abortCases() {
    for (int scenario = 0; scenario < 5; ++scenario) {
        Capture capture;
        MutexService service(twoNodes(), 0, [&](uint32_t peer, const Message& message) {
            if (scenario == 0) throw std::runtime_error("Injected write failure");
            if (scenario == 4) throw std::runtime_error("");
            capture.send(peer, message);
        });
        service.start();
        auto call = std::async(std::launch::async, [&] { service.cs_enter(); });
        try {
            if (scenario != 0 && scenario != 4) capture.take(MessageType::Request);
            if (scenario == 1) service.onNetworkError(1, "Injected disconnect");
            if (scenario == 2) service.stop();
            if (scenario == 3) service.onMessage({MessageType::Locked, 99, 0, 1});
            ready(service, call);
            const std::string expected[] = {"write failure", "disconnect", "not running", "Unknown node", "service failed"};
            mustFail(call, expected[scenario]);
        } catch (...) {
            service.stop();
            throw;
        }
    }
    MutexService overflow(twoNodes(), 0, [](uint32_t, const Message&) {});
    overflow.onMessage({MessageType::Request, 1, 1, UINT64_MAX});
    overflow.start();
    auto call = std::async(std::launch::async, [&] { overflow.cs_enter(); });
    ready(overflow, call);
    mustFail(call, "sequence exhausted");
}

void singleNode() {
    Configuration config{{1, 0, 0, 100}, {{7, "localhost", 24000, {7}}}};
    MutexService service(config, 7, [](uint32_t, const Message&) { throw std::runtime_error("Self-message used socket"); });
    service.start();
    auto call = std::async(std::launch::async, [&] {
        for (unsigned i = 0; i < 100; ++i) { service.cs_enter(); service.cs_leave(); }
    });
    ready(service, call);
    call.get();
}

void concurrentNodes(const Configuration& config) {
    std::map<uint32_t, std::unique_ptr<MutexService>> services;
    for (const auto& node : config.nodes) {
        services.emplace(node.nodeId, std::make_unique<MutexService>(config, node.nodeId,
            [&](uint32_t peer, const Message& message) { services.at(peer)->onMessage(message); }));
    }
    for (auto& entry : services) entry.second->start();
    std::atomic<unsigned> active{0};
    std::atomic<unsigned> completed{0};
    std::vector<std::future<void>> calls;
    for (auto& entry : services) {
        auto* service = entry.second.get();
        calls.push_back(std::async(std::launch::async, [&, service] {
            for (unsigned i = 0; i < 20; ++i) {
                service->cs_enter();
                require(active.fetch_add(1) == 0, "Concurrent service entries overlapped");
                std::this_thread::sleep_for(100us);
                active.fetch_sub(1);
                service->cs_leave();
                ++completed;
            }
        }));
    }
    try {
        for (auto& call : calls) {
            require(call.wait_for(5s) == std::future_status::ready, "Concurrent services timed out");
            call.get();
        }
        require(completed == config.nodes.size() * 20, "Wrong service completion count");
    } catch (...) {
        for (auto& entry : services) entry.second->stop();
        throw;
    }
    for (auto& entry : services) entry.second->stop();
}
}

int main() {
    try {
        blockingAndClock();
        abortCases();
        singleNode();
        concurrentNodes({{3, 0, 1, 20}, {{0, "localhost", 24000, {0,1}},
            {1, "localhost", 24001, {1,2}}, {2, "localhost", 24002, {0,2}}}});
        concurrentNodes({{2, 0, 1, 20}, {{0, "localhost", 24000, {1}}, {1, "localhost", 24001, {1}}}});
        std::cout << "PASS: blocking API, voter progress, sequence clock, aborts, optional self-voting, 100 concurrent entries\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
