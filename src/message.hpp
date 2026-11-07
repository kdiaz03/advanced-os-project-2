#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

enum class MessageType : uint32_t {
    Ping = 1,
    Done = 2,
    Request = 3,
    Locked = 4,
    Failed = 5,
    Inquire = 6,
    Relinquish = 7,
    Release = 8,
    Ready = 9,
    Goodbye = 10,
    ObserveEnter = 11,
    ObserveExit = 12,
    EnterAck = 13,
    ExitAck = 14
};

// sequence identifies a mutex request, a checker request number, or a control count.
// sender identifies the transmitting node; requester identifies the request being discussed.
struct Message {
    MessageType type;
    uint32_t senderId;
    uint32_t requesterId;
    uint64_t sequence;
};

// smaller sequence numbers win; node ids break equal-sequence ties.
struct RequestId {
    uint64_t sequence;
    uint32_t nodeId;

    bool operator<(const RequestId& other) const {
        return sequence < other.sequence || (sequence == other.sequence && nodeId < other.nodeId);
    }
    bool operator==(const RequestId& other) const {
        return sequence == other.sequence && nodeId == other.nodeId;
    }
};

// the destination is routing information and is not part of the wire frame.
struct OutgoingMessage {
    uint32_t peerId;
    Message message;
};

// fixed-size frames use explicit big-endian fields, never native struct layout.
constexpr size_t messageBytes = 24;
using MessageFrame = std::array<uint8_t, messageBytes>;
MessageFrame encodeMessage(const Message& message);
Message decodeMessage(const MessageFrame& frame);
bool isObserverMessage(MessageType type);

#endif
