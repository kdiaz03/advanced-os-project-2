#include "message.hpp"

#include <iostream>
#include <stdexcept>

int main() {
    try {
        const Message original{MessageType::Ping, 0x11223344, 0xaabbccdd, 0x0102030405060708ULL};
        const MessageFrame expected{0x50, 0x32, 0x4d, 0x31, 0, 0, 0, 1,
            0x11, 0x22, 0x33, 0x44, 0xaa, 0xbb, 0xcc, 0xdd, 1, 2, 3, 4, 5, 6, 7, 8};
        if (encodeMessage(original) != expected) throw std::runtime_error("Wire format differs");
        const auto decoded = decodeMessage(expected);
        if (decoded.sequence != original.sequence || decoded.requesterId != original.requesterId ||
                decoded.senderId != original.senderId || decoded.type != original.type) {
            throw std::runtime_error("Decoded fields differ");
        }
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
