#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include "mutex_service.hpp"
#include "observer.hpp"

#include <chrono>
#include <random>

std::chrono::steady_clock::duration sampleDelay(double meanMs, std::mt19937_64& random);
void runApplication(const Configuration& config, uint32_t nodeId, MutexService& service, Observer& observer);

#endif
