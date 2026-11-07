#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "mutex_service.hpp"
#include "observer.hpp"

#include <chrono>
#include <random>

// convert a sampled millisecond delay into the local monotonic clock's duration.
std::chrono::steady_clock::duration sampleDelay(double meanMs, std::mt19937_64& random);
// generate the configured requests using only the service and checker interfaces.
void runApplication(const Configuration& config, uint32_t nodeId, MutexService& service, Observer& observer);

#endif
