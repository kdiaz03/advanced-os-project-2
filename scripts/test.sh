#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
cd "$PROJECT_DIR"
./scripts/build.sh configs/local-3nodes.txt
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc \
    tests/config_tests.cpp src/config.cpp -o build/config_tests
./build/config_tests
python3 tests/cli_tests.py
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc \
    tests/message_tests.cpp src/message.cpp -o build/message_tests
./build/message_tests
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc \
    tests/voter_tests.cpp src/voter.cpp -o build/voter_tests
./build/voter_tests
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc \
    tests/protocol_tests.cpp src/requester.cpp src/voter.cpp -o build/protocol_tests
./build/protocol_tests
service_sources=(src/config.cpp src/requester.cpp src/voter.cpp src/mutex_service.cpp)
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc \
    tests/service_tests.cpp "${service_sources[@]}" -o build/service_tests
./build/service_tests
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc \
    tests/application_tests.cpp "${service_sources[@]}" src/application.cpp src/run_control.cpp src/observer.cpp -o build/application_tests
./build/application_tests
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc \
    tests/observer_tests.cpp src/config.cpp src/observer.cpp -o build/observer_tests
./build/observer_tests
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc \
    tests/network_node.cpp src/config.cpp src/message.cpp src/network.cpp -o build/network_node
python3 tests/network_tests.py
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc \
    tests/service_node.cpp "${service_sources[@]}" src/network.cpp src/message.cpp -o build/service_node
python3 tests/service_network_tests.py
python3 tests/run_tests.py
