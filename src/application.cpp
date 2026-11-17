#include "application.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

std::chrono::steady_clock::duration sampleDelay(double meanMs, std::mt19937_64& random) {
    using Clock = std::chrono::steady_clock;
    if (meanMs == 0) return Clock::duration::zero();
    // scale a unit-mean exponential sample to the configured mean in milliseconds.
    const long double milliseconds = std::exponential_distribution<double>(1.0)(random) * static_cast<long double>(meanMs);
    const auto maximum = std::chrono::duration<long double, std::milli>(Clock::duration::max()).count() / 2;
    if (!std::isfinite(milliseconds) || milliseconds < 0 || milliseconds >= maximum) {
        throw std::runtime_error("Sampled delay exceeds the clock range");
    }
    return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<long double, std::milli>(milliseconds));
}

ApplicationStats runApplication(const Configuration& config, uint32_t nodeId, MutexService& service, Observer& observer) {
    using Clock = std::chrono::steady_clock;
    std::random_device seed;
    // mix the node id into independent seeds so nodes do not share a timing sequence.
    std::seed_seq seeds{seed(), seed(), nodeId};
    std::mt19937_64 random(seeds);
    using Ms = std::chrono::duration<double, std::milli>;
    ApplicationStats stats;
    const auto start = Clock::now();
    for (uint64_t request = 1; request <= config.params.requestsPerNode; ++request) {
        const auto requested = Clock::now();
        service.cs_enter();
        const auto grant = service.grantTime();
        const auto delay = sampleDelay(config.params.meanInterRequestMs, random);
        const auto duration = sampleDelay(config.params.meanCsMs, random);
        // registration must be acknowledged before the instrumented cs body begins.
        observer.enter(request);
        std::cout << "CS_ENTER node=" << nodeId << " request=" << request << '\n';
        std::this_thread::sleep_for(duration);
        // response time ends when the cs body is over, before the observer exit exchange.
        const double response = Ms(Clock::now() - requested).count();
        stats.responseTotalMs += response;
        stats.responseMaxMs = std::max(stats.responseMaxMs, response);
        ++stats.requests;
        std::cout << "CS_EXIT node=" << nodeId << " request=" << request << '\n';
        // remove the observation before releasing votes, so delayed exits cannot cause false overlaps.
        observer.leave(request);
        service.cs_leave();
        if (request < config.params.requestsPerNode) {
            // the delay starts at the grant; an expired deadline adds no extra wait.
            const auto deadline = std::max(grant + delay, Clock::now());
            // check for run failure periodically during a long inter-request delay.
            while (Clock::now() < deadline) {
                service.checkHealth();
                std::this_thread::sleep_until(std::min(deadline, Clock::now() + std::chrono::milliseconds(100)));
            }
        }
    }
    stats.durationMs = Ms(Clock::now() - start).count();
    std::cout << "Completed " << config.params.requestsPerNode << " requests on node " << nodeId << ".\n";
    return stats;
}
