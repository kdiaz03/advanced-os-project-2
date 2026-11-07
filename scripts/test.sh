#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
cd "$PROJECT_DIR"
./scripts/build.sh configs/local-3nodes.txt
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread -Isrc \
    tests/config_tests.cpp src/config.cpp -o build/config_tests
./build/config_tests
python3 tests/cli_tests.py
