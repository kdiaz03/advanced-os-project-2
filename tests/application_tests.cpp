#include "application.hpp"
#include "run_control.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        std::mt19937_64 random(12345);
        if (sampleDelay(0, random).count() != 0) throw std::runtime_error("Zero mean added a delay");
        double sum = 0;
        double squares = 0;
        for (unsigned i = 0; i < 100000; ++i) {
            const double value = std::chrono::duration<double, std::milli>(sampleDelay(10, random)).count();
            if (value < 0) throw std::runtime_error("Negative delay");
            sum += value;
            squares += value * value;
        }
        const double mean = sum / 100000;
        const double variance = squares / 100000 - mean * mean;
        if (std::abs(mean - 10) > 0.2 || std::abs(variance - 100) > 5) {
            throw std::runtime_error("Delay samples do not have the expected exponential mean and variance");
        }
        const Configuration config{{1, 0, 0, 3}, {{7, "localhost", 24000, {7}}}};
        RunControl control(config);
        bool rejected = false;
        try { control.onMessage({MessageType::Goodbye, 7, 7, 0}); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Accepted premature shutdown");
        control.onMessage({MessageType::Ready, 7, 7, 0});
        rejected = false;
        try { control.onMessage({MessageType::Done, 7, 7, 2}); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Accepted wrong completion count");
        control.onMessage({MessageType::Done, 7, 7, 3});
        control.onMessage({MessageType::Goodbye, 7, 7, 0});
        std::cout << "PASS: exponential delay samples and run control validation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
