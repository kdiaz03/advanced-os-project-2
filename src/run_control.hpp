#ifndef RUN_CONTROL_HPP
#define RUN_CONTROL_HPP

#include "mutex_service.hpp"

#include <condition_variable>
#include <map>
#include <mutex>
#include <set>

// track run-wide barriers after the protocol worker has processed each marker.
class RunControl {
public:
    explicit RunControl(const Configuration& config) : config_(config) {}
    void onMessage(const Message& message);
    void waitFor(MessageType type, MutexService& service);

private:
    Configuration config_;
    std::mutex mutex_;
    std::condition_variable cv_;
    // count distinct node ids rather than allowing duplicate markers to complete a barrier.
    std::map<MessageType, std::set<uint32_t>> received_;
};

#endif
