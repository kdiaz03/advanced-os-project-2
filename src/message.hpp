#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

enum class MessageType : uint32_t {
    Ping = 1,
    Done = 2
};

struct Message {
    MessageType type;
    uint32_t senderId;
    uint32_t requesterId;
    uint64_t sequence;
};

// Fixed-size frames use explicit big-endian fields, never native struct layout.
constexpr size_t messageBytes = 24;
using MessageFrame = std::array<uint8_t, messageBytes>;
MessageFrame encodeMessage(const Message& message);
Message decodeMessage(const MessageFrame& frame);

#endif
