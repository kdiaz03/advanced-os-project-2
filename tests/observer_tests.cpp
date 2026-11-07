#include "observer.hpp"

#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
Configuration config(uint32_t requests = 1) {
    return {{2, 0, 0, requests}, {{4, "localhost", 24004, {4,9}}, {9, "localhost", 24009, {4,9}}}};
}

void require(bool condition, const std::string& text) {
    if (!condition) throw std::runtime_error(text);
}

void serverCases() {
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        std::ostringstream log;
        std::vector<Message> acknowledgments;
        Observer observer(config(), 4, [&](uint32_t peer, const Message& message) {
            require(peer == 9, "Wrong acknowledgment peer");
            acknowledgments.push_back(message);
        }, log);
        observer.enter(1);
        if (scenario != 1) observer.leave(1);
        if (scenario == 4) observer.onMessage({MessageType::ObserveEnter, 9, 9, 2});
        observer.onMessage({MessageType::ObserveEnter, 9, 9, 1});
        require(acknowledgments.back().type == MessageType::EnterAck, "Entry acknowledgment missing");
        if (scenario == 5) observer.onMessage({MessageType::ObserveEnter, 9, 9, 1});
        if (scenario == 3) observer.onMessage({MessageType::ObserveExit, 9, 9, 2});
        if (scenario != 2) observer.onMessage({MessageType::ObserveExit, 9, 9, 1});
        if (scenario == 1) observer.leave(1);
        const bool passed = observer.finish();
        require(passed == (scenario == 0), "Wrong observer result");
        if (scenario == 1) require(log.str().find("overlap node=9 request=1 while node=4 request=1") != std::string::npos,
                                   "Overlap did not identify both requests");
        if (scenario == 2) require(log.str().find("critical section still active") != std::string::npos,
                                   "Missing exit was not detected");
    }
    std::ostringstream log;
    Observer empty(config(0), 4, [](uint32_t, const Message&) {}, log);
    require(empty.finish(), "Zero-request run should pass");
}

void clientCases() {
    using namespace std::chrono_literals;
    std::ostringstream log;
    std::promise<void> sent;
    Observer client(config(), 9, [&](uint32_t peer, const Message& message) {
        require(peer == 4 && message.type == MessageType::ObserveEnter, "Wrong client event");
        sent.set_value();
    }, log);
    auto waiting = std::async(std::launch::async, [&] {
        try { client.enter(1); }
        catch (const std::runtime_error& error) { return std::string(error.what()); }
        return std::string("Unexpected success");
    });
    require(sent.get_future().wait_for(1s) == std::future_status::ready, "Client sent no entry");
    require(waiting.wait_for(0s) != std::future_status::ready, "Client entered without acknowledgment");
    client.onNetworkError(4, "lost connection");
    require(waiting.wait_for(1s) == std::future_status::ready, "Disconnect did not wake client");
    require(waiting.get().find("lost connection") != std::string::npos, "Disconnect was not reported");

    Observer timeout(config(), 9, [](uint32_t, const Message&) {}, log, 20ms);
    bool failed = false;
    try { timeout.enter(1); }
    catch (const std::runtime_error& error) { failed = std::string(error.what()).find("timed out") != std::string::npos; }
    require(failed, "Missing acknowledgment was accepted");
}
}

int main() {
    try {
        serverCases();
        clientCases();
        std::cout << "PASS: observer overlap, counts, pairing, duplicate events, disconnects, and missing acknowledgments\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
