#include "message.hpp"

#include <iostream>
#include <stdexcept>

int main() {
    try {
        // use different byte values to expose field-order and endian mistakes.
        const Message original{MessageType::Ping, 0x11223344, 0xaabbccdd, 0x0102030405060708ULL};
        const MessageFrame expected{0x50, 0x32, 0x4d, 0x31, 0, 0, 0, 1,
            0x11, 0x22, 0x33, 0x44, 0xaa, 0xbb, 0xcc, 0xdd, 1, 2, 3, 4, 5, 6, 7, 8};
        if (encodeMessage(original) != expected) throw std::runtime_error("Wire format differs");
        const auto decoded = decodeMessage(expected);
        if (decoded.sequence != original.sequence || decoded.requesterId != original.requesterId ||
                decoded.senderId != original.senderId || decoded.type != original.type) {
            throw std::runtime_error("Decoded fields differ");
        }
        // keep a sequence above 32 bits to catch accidental narrowing.
        for (const auto type : {MessageType::Request, MessageType::Locked, MessageType::Failed,
                MessageType::Inquire, MessageType::Relinquish, MessageType::Release,
                MessageType::ObserveEnter, MessageType::ObserveExit, MessageType::EnterAck, MessageType::ExitAck}) {
            const Message message{type, 13, 7, 0x100000001ULL};
            const auto result = decodeMessage(encodeMessage(message));
            if (result.type != type || result.sequence != message.sequence || result.requesterId != 7) {
                throw std::runtime_error("Protocol frame differs");
            }
        }
        // corrupt the wire version and message type independently.
        for (size_t offset : {size_t(0), size_t(7)}) {
            auto invalid = expected;
            invalid[offset] = 0xff;
            bool rejected = false;
            try { decodeMessage(invalid); }
            catch (const std::runtime_error&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Accepted invalid frame");
        }
        std::cout << "PASS: message wire format\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
