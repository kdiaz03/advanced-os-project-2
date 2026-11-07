#include "application.hpp"
#include "network.hpp"
#include "run_control.hpp"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
const char* usage = "Usage: ./build/main -c <config> -n <node_id> [-r <run_id>]\n"
                    "       ./build/main -c <config> --check-config | --list-nodes";

struct Options {
    std::string configPath;
    std::optional<uint32_t> nodeId;
    std::string runId = "manual";
    bool check = false;
    bool list = false;
};

// validation and node-list modes let scripts reuse the same configuration parser.
Options parseOptions(int argc, char* argv[]) {
    Options options;
    bool hasRunId = false;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "-c" && i + 1 < argc && options.configPath.empty()) options.configPath = argv[++i];
        else if (option == "-n" && i + 1 < argc && !options.nodeId) options.nodeId = parseUnsigned(argv[++i], "Node ID");
        else if (option == "-r" && i + 1 < argc && !hasRunId) { options.runId = argv[++i]; hasRunId = true; }
        else if (option == "--check-config" && !options.check) options.check = true;
        else if (option == "--list-nodes" && !options.list) options.list = true;
        else throw std::runtime_error(usage);
    }
    if (options.configPath.empty() || (options.check && options.list) ||
            (!options.check && !options.list && !options.nodeId) ||
            ((options.check || options.list) && hasRunId) || (options.list && options.nodeId)) {
        throw std::runtime_error(usage);
    }
    return options;
}

void run(const Configuration& config, const Options& options, const std::filesystem::path& projectDir) {
    const uint32_t nodeId = *options.nodeId;
    RunControl control(config);
    Network network(config, nodeId, options.runId);
    // keep observer records separate from application output produced by other threads.
    std::ofstream observerLog;
    if (nodeId == config.nodes.front().nodeId) {
        const auto directory = projectDir / "logs" / options.runId;
        std::filesystem::create_directories(directory);
        observerLog.open(directory / "observer.log");
        if (!observerLog) throw std::runtime_error("Cannot open observer log");
    }
    Observer observer(config, nodeId,
        [&](uint32_t peer, const Message& message) { network.sendMessage(peer, message); }, observerLog);
    MutexService service(config, nodeId,
        [&](uint32_t peer, const Message& message) { network.sendMessage(peer, message); },
        [&](const Message& message) { control.onMessage(message); });
    try {
        network.establishConnections();
        service.start();
        // route checker events separately; all mutex and run-control events use the worker.
        network.startReceiving([&](const Message& message) {
            if (isObserverMessage(message.type)) observer.onMessage(message);
            else service.onMessage(message);
        }, [&](uint32_t peer, const std::string& error) {
            observer.onNetworkError(peer, error);
            service.onNetworkError(peer, error);
        });
        // wait for every node to finish startup before generating application requests.
        service.broadcastControl(MessageType::Ready);
        control.waitFor(MessageType::Ready, service);
        runApplication(config, nodeId, service, observer);
        service.broadcastControl(MessageType::Done, config.params.requestsPerNode);
        // done shares each requester's fifo stream, so earlier releases are processed first.
        control.waitFor(MessageType::Done, service);
        // a failed check prevents this node from sending a successful shutdown marker.
        if (nodeId == observer.nodeId()) {
            const bool passed = observer.finish();
            observerLog.flush();
            if (!observerLog) throw std::runtime_error("Cannot write observer results");
            if (!passed) throw std::runtime_error("Mutual exclusion check failed; see observer.log");
        }
        service.broadcastControl(MessageType::Goodbye);
        control.waitFor(MessageType::Goodbye, service);
        service.stop();
        network.stop();
        std::cout << "[Node " << nodeId << "] Shutdown complete.\n";
    } catch (...) {
        // join producers and receivers while their callback targets are still alive.
        service.stop();
        network.stop();
        throw;
    }
}
}

int main(int argc, char* argv[]) {
    std::cout.setf(std::ios::unitbuf);
    try {
        const auto options = parseOptions(argc, argv);
        const auto config = parseConfigurationFile(options.configPath);
        if (options.nodeId) findNode(config, *options.nodeId);
        if (options.list) {
            for (const auto& node : config.nodes) std::cout << node.nodeId << '\t' << node.hostName << '\t' << node.port << '\n';
        } else if (options.check) std::cout << "Configuration valid.\n";
        else run(config, options, std::filesystem::canonical(argv[0]).parent_path().parent_path());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
