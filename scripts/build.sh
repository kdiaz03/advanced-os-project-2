#!/usr/bin/env bash
set -euo pipefail
PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
if (( $# != 1 )); then
    echo "Usage: $0 <config_file>" >&2
    exit 1
fi
[[ -r $1 ]] || { echo "Cannot read config: $1" >&2; exit 1; }
mkdir -p "$PROJECT_DIR/build"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread \
    "$PROJECT_DIR"/src/*.cpp -o "$PROJECT_DIR/build/main"
echo "Built $PROJECT_DIR/build/main"
