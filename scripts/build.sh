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
# default to the system g++: on the dc machines, c++ is a newer gcc whose binaries
# need a newer libstdc++ than the one installed in /lib64.
if [[ -z ${CXX:-} ]]; then
    if [[ -x /usr/bin/g++ ]]; then CXX=/usr/bin/g++; else CXX=c++; fi
fi
echo "Compiler: $CXX"
"$CXX" -std=c++17 -Wall -Wextra -Wpedantic -pthread \
    "$PROJECT_DIR"/src/*.cpp -o "$PROJECT_DIR/build/main"
# validate the config with the program itself rather than a separate shell parser.
"$PROJECT_DIR/build/main" -c "$CONFIG_FILE" --check-config
echo "Built $PROJECT_DIR/build/main"
