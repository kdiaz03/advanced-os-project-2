#include "network.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <exception>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr size_t maxGreetingBytes = 1024 * 1024;

std::runtime_error socketError(const std::string& operation) {
    return std::runtime_error(operation + ": " + std::strerror(errno));
}

// own temporary descriptors until a completed handshake transfers them to a peer.
struct Socket {
    int fd;
    explicit Socket(int value) : fd(value) {}
    ~Socket() { if (fd >= 0) close(fd); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    int release() { const int result = fd; fd = -1; return result; }
};

void prepareSocket(int fd) {
    if (fd < 0) throw socketError("socket");
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) throw socketError("fcntl");
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) {
        throw socketError("SO_NOSIGPIPE");
    }
#endif
}

std::string greeting(const Configuration& config, const std::string& runId) {
    if (runId.empty() || runId.size() > 128 || runId.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.") != std::string::npos) {
        throw std::runtime_error("Run ID must contain 1..128 letters, digits, dots, underscores, or hyphens");
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "P2/1\n" << runId << '\n' << std::setprecision(17)
        << config.params.numNodes << ' ' << config.params.meanInterRequestMs << ' '
        << config.params.meanCsMs << ' ' << config.params.requestsPerNode << '\n';
    for (const auto& node : config.nodes) {
        out << node.nodeId << ' ' << node.hostName << ' ' << node.port << '\n';
        for (uint32_t member : node.quorum) out << member << ' ';
        out << '\n';
    }
    const auto result = out.str();
    if (result.size() > maxGreetingBytes) throw std::runtime_error("Handshake config is too large");
    return result;
}
}

Network::Network(const Configuration& config, uint32_t nodeId, const std::string& runId,
                 std::chrono::milliseconds ioTimeout)
    : config_(config), nodeId_(nodeId), greeting_(greeting(config, runId)), ioTimeout_(ioTimeout) {
    findNode(config_, nodeId_);
    if (ioTimeout_.count() <= 0) throw std::runtime_error("I/O timeout must be positive");
    for (const auto& node : config_.nodes) {
        if (node.nodeId != nodeId_) peers_.try_emplace(node.nodeId);
    }
}

Network::~Network() { stop(); }

void Network::waitFor(int fd, short events, Deadline deadline) {
    while (!stopping_.load() && !setupCancelled_.load()) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) throw std::runtime_error("Socket operation timed out");
        pollfd descriptor{fd, events, 0};
        const int result = poll(&descriptor, 1, static_cast<int>(std::min<int64_t>(remaining, 100)));
        if (result < 0) {
            if (errno == EINTR) continue;
            throw socketError("poll");
        }
        if (result > 0) {
            if (descriptor.revents & POLLNVAL) throw std::runtime_error("Invalid socket descriptor");
            return; // recv/send reports eof or socket errors as well as readiness.
        }
    }
    throw std::runtime_error("Network stopped");
}

void Network::readExact(int fd, uint8_t* data, size_t size, Deadline deadline) {
    size_t offset = 0;
    while (offset < size) {
        waitFor(fd, POLLIN, deadline);
        const ssize_t count = recv(fd, data + offset, size - offset, 0);
        if (count == 0) throw std::runtime_error("Peer closed connection during read");
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            throw socketError("recv");
        }
        offset += static_cast<size_t>(count);
    }
}

void Network::writeExact(int fd, const uint8_t* data, size_t size, Deadline deadline) {
    size_t offset = 0;
    while (offset < size) {
        waitFor(fd, POLLOUT, deadline);
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const ssize_t count = send(fd, data + offset, size - offset, flags);
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            throw socketError("send");
        }
        if (count == 0) throw std::runtime_error("send returned zero bytes");
        offset += static_cast<size_t>(count);
    }
}

void Network::writeGreeting(int fd, Deadline deadline) {
    const std::array<uint32_t, 2> header{htonl(nodeId_), htonl(static_cast<uint32_t>(greeting_.size()))};
    writeExact(fd, reinterpret_cast<const uint8_t*>(header.data()), sizeof(header), deadline);
    writeExact(fd, reinterpret_cast<const uint8_t*>(greeting_.data()), greeting_.size(), deadline);
}

uint32_t Network::readGreeting(int fd, Deadline deadline) {
    std::array<uint32_t, 2> header{};
    readExact(fd, reinterpret_cast<uint8_t*>(header.data()), sizeof(header), deadline);
    const uint32_t size = ntohl(header[1]);
    if (size == 0 || size > maxGreetingBytes) throw std::runtime_error("Invalid handshake size");
    std::string received(size, '\0');
    readExact(fd, reinterpret_cast<uint8_t*>(received.data()), size, deadline);
    if (received != greeting_) throw std::runtime_error("Handshake run/config mismatch");
    return ntohl(header[0]);
}

void Network::openListener() {
    Socket listener(socket(AF_INET, SOCK_STREAM, 0));
    prepareSocket(listener.fd);
    const int enabled = 1;
    if (setsockopt(listener.fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0) {
        throw socketError("SO_REUSEADDR");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(findNode(config_, nodeId_).port);
    if (bind(listener.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) throw socketError("bind");
    if (listen(listener.fd, SOMAXCONN) < 0) throw socketError("listen");
    listenFd_ = listener.release();
}

void Network::acceptPeers(Deadline deadline) {
    size_t expected = 0;
    for (const auto& entry : peers_) if (entry.first < nodeId_) ++expected;
    for (size_t accepted = 0; accepted < expected;) {
        waitFor(listenFd_, POLLIN, deadline);
        Socket incoming(accept(listenFd_, nullptr, nullptr));
        if (incoming.fd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            throw socketError("accept");
        }
        prepareSocket(incoming.fd);
        const auto handshakeDeadline = std::min(deadline, std::chrono::steady_clock::now() + ioTimeout_);
        const uint32_t peerId = readGreeting(incoming.fd, handshakeDeadline);
        auto peer = peers_.find(peerId);
        if (peerId >= nodeId_ || peer == peers_.end() || peer->second.fd >= 0) {
            throw std::runtime_error("Unexpected or duplicate incoming peer ID");
        }
        writeGreeting(incoming.fd, handshakeDeadline);
        peer->second.fd = incoming.release();
        ++accepted;
    }
}

void Network::connectPeers(Deadline deadline) {
    for (auto& entry : peers_) {
        const uint32_t peerId = entry.first;
        if (peerId < nodeId_) continue;
        const auto& node = findNode(config_, peerId);
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* addresses = nullptr;
        const int result = getaddrinfo(node.hostName.c_str(), std::to_string(node.port).c_str(), &hints, &addresses);
        if (result != 0) throw std::runtime_error("Cannot resolve " + node.hostName + ": " + gai_strerror(result));
        std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> ownedAddresses(addresses, freeaddrinfo);
        while (entry.second.fd < 0) {
            if (stopping_.load() || setupCancelled_.load()) throw std::runtime_error("Network stopped");
            if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("Connection startup timed out");
            for (const addrinfo* address = addresses; address; address = address->ai_next) {
                Socket outgoing(socket(address->ai_family, address->ai_socktype, address->ai_protocol));
                prepareSocket(outgoing.fd);
                int connected = connect(outgoing.fd, address->ai_addr, address->ai_addrlen);
                if (connected < 0 && errno == EINPROGRESS) {
                    waitFor(outgoing.fd, POLLOUT, deadline);
                    int error = 0;
                    socklen_t length = sizeof(error);
                    if (getsockopt(outgoing.fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0) throw socketError("SO_ERROR");
                    connected = error == 0 ? 0 : -1;
                }
                if (connected != 0) continue;
                const auto handshakeDeadline = std::min(deadline, std::chrono::steady_clock::now() + ioTimeout_);
                writeGreeting(outgoing.fd, handshakeDeadline);
                if (readGreeting(outgoing.fd, handshakeDeadline) != peerId) {
                    throw std::runtime_error("Unexpected outgoing peer ID");
                }
                entry.second.fd = outgoing.release();
                break;
            }
            if (entry.second.fd < 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

void Network::establishConnections(std::chrono::milliseconds timeout) {
    if (setupAttempted_ || stopping_.load()) throw std::runtime_error("Network setup may only run once");
    setupAttempted_ = true;
    if (timeout.count() <= 0) throw std::runtime_error("Startup timeout must be positive");
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::thread acceptThread;
    std::exception_ptr firstError;
    std::mutex errorMutex;
    const auto recordError = [&] {
        std::lock_guard<std::mutex> lock(errorMutex);
        if (!firstError) firstError = std::current_exception();
        setupCancelled_.store(true);
    };
    try {
        openListener();
        acceptThread = std::thread([&] {
            try { acceptPeers(deadline); }
            catch (...) { recordError(); }
        });
        connectPeers(deadline);
    } catch (...) {
        recordError();
    }
    if (acceptThread.joinable()) acceptThread.join();
    if (firstError) {
        stop();
        std::rethrow_exception(firstError);
    }
    close(listenFd_);
    listenFd_ = -1;
    established_ = true;
}

void Network::sendMessage(uint32_t peerId, const Message& message) {
    if (!established_ || stopping_.load()) throw std::runtime_error("Network is not running");
    if (message.senderId != nodeId_) throw std::runtime_error("Outgoing sender ID mismatch");
    auto peer = peers_.find(peerId);
    if (peer == peers_.end()) throw std::runtime_error("No remote peer: " + std::to_string(peerId));
    const auto frame = encodeMessage(message);
    // one entire frame per writer. the protocol worker will determine send order.
    std::lock_guard<std::mutex> lock(peer->second.writeMutex);
    writeExact(peer->second.fd, frame.data(), frame.size(), std::chrono::steady_clock::now() + ioTimeout_);
}

void Network::receiveLoop(uint32_t peerId, ReceiveCallback onMessage, ErrorCallback onError) {
    try {
        const int fd = peers_.at(peerId).fd;
        while (!stopping_.load()) {
            // idle peers have no receive deadline; a started frame must finish promptly.
            waitFor(fd, POLLIN, Deadline::max());
            MessageFrame frame{};
            readExact(fd, frame.data(), frame.size(), std::chrono::steady_clock::now() + ioTimeout_);
            const auto message = decodeMessage(frame);
            if (message.senderId != peerId) throw std::runtime_error("Incoming sender ID mismatch");
            onMessage(message);
            // goodbye is the final frame on this peer's stream.
            if (message.type == MessageType::Goodbye) return;
        }
    } catch (const std::exception& error) {
        if (!stopping_.load()) onError(peerId, error.what());
    }
}

void Network::startReceiving(ReceiveCallback onMessage, ErrorCallback onError) {
    if (!established_ || receiversStarted_ || stopping_.load() || !onMessage || !onError) {
        throw std::runtime_error("Cannot start receivers");
    }
    receiversStarted_ = true;
    try {
        for (const auto& entry : peers_) {
            receivers_.emplace_back(&Network::receiveLoop, this, entry.first, onMessage, onError);
        }
    } catch (...) {
        stop();
        throw;
    }
}

void Network::stop() {
    stopping_.store(true);
    for (auto& entry : peers_) if (entry.second.fd >= 0) shutdown(entry.second.fd, SHUT_RDWR);
    for (auto& receiver : receivers_) if (receiver.joinable()) receiver.join();
    receivers_.clear();
    for (auto& entry : peers_) {
        if (entry.second.fd >= 0) close(entry.second.fd);
        entry.second.fd = -1;
    }
    if (listenFd_ >= 0) close(listenFd_);
    listenFd_ = -1;
}
