#include "message.hpp"

#include <stdexcept>

namespace {
constexpr uint32_t magic = 0x50324d31; // p2m1: project and wire version

void put(MessageFrame& frame, size_t offset, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) {
        frame[offset + bytes - i - 1] = static_cast<uint8_t>(value & 0xff);
        value >>= 8;
    }
}

uint64_t get(const MessageFrame& frame, size_t offset, size_t bytes) {
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i) value = (value << 8) | frame[offset + i];
    return value;
}

void validateType(MessageType type) {
    switch (type) {
        case MessageType::Ping:
        case MessageType::Done:
        case MessageType::Request:
        case MessageType::Locked:
        case MessageType::Failed:
        case MessageType::Inquire:
        case MessageType::Relinquish:
        case MessageType::Release:
            return;
    }
    throw std::runtime_error("Unknown message type");
}
}

MessageFrame encodeMessage(const Message& message) {
    validateType(message.type);
    MessageFrame frame{};
    put(frame, 0, magic, 4);
    put(frame, 4, static_cast<uint32_t>(message.type), 4);
    put(frame, 8, message.senderId, 4);
    put(frame, 12, message.requesterId, 4);
    put(frame, 16, message.sequence, 8);
    return frame;
}

Message decodeMessage(const MessageFrame& frame) {
    if (get(frame, 0, 4) != magic) throw std::runtime_error("Invalid message version");
    Message message{static_cast<MessageType>(get(frame, 4, 4)),
        static_cast<uint32_t>(get(frame, 8, 4)), static_cast<uint32_t>(get(frame, 12, 4)), get(frame, 16, 8)};
    validateType(message.type);
    return message;
}
