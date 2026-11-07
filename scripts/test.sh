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
    tests/network_node.cpp src/config.cpp src/message.cpp src/network.cpp -o build/network_node
python3 tests/network_tests.py
