#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
if (( $# != 1 )); then
    echo "Usage: $0 <config_file>" >&2
    exit 1
fi
load_config_path "$1"
[[ -r $CONFIG_FILE ]] || { echo "Cannot read config: $1" >&2; exit 1; }
mkdir -p "$PROJECT_DIR/build"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wpedantic -pthread \
    "$PROJECT_DIR"/src/*.cpp -o "$PROJECT_DIR/build/main"
"$PROJECT_DIR/build/main" -c "$CONFIG_FILE" --check-config
echo "Built $PROJECT_DIR/build/main"
