#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "mutex_service.hpp"
#include "observer.hpp"

#include <chrono>
#include <random>

// convert a sampled millisecond delay into the local monotonic clock's duration.
std::chrono::steady_clock::duration sampleDelay(double meanMs, std::mt19937_64& random);
// per-node measurements; every interval uses this node's monotonic clock only.
struct ApplicationStats {
    uint64_t requests = 0;
    // from calling cs_enter() until the critical section body finishes.
    double responseTotalMs = 0;
    double responseMaxMs = 0;
    // from the first request until the last cs_leave() returns.
    double durationMs = 0;
};

// generate the configured requests using only the service and checker interfaces.
ApplicationStats runApplication(const Configuration& config, uint32_t nodeId, MutexService& service, Observer& observer);

#endif
