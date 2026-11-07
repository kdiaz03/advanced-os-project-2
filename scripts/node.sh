#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
(( $# == 3 )) || { echo "Expected operation, run ID, node ID." >&2; exit 1; }
operation=$1
run_id=$2
node_id=$3
[[ $run_id =~ ^[A-Za-z0-9._-]+$ && $run_id != .* && $node_id =~ ^[0-9]+$ ]] || exit 1
run_dir="$PROJECT_DIR/logs/$run_id"
binary="$PROJECT_DIR/build/main"
config_file="$run_dir/config.txt"
pid_file="$run_dir/node_${node_id}.pid"
gate="$run_dir/node_${node_id}.gate"
expected="$binary -c $config_file -n $node_id -r $run_id"

if [[ $operation == check ]]; then
    [[ -x $binary && -r $config_file ]] || { echo "Missing shared binary or config on $(hostname)." >&2; exit 1; }
    "$binary" -c "$config_file" -n "$node_id" --check-config > /dev/null
    exit 0
fi
[[ $operation == run || $operation == cleanup ]] || exit 1

# serialize process registration with cleanup so a late launch cannot escape it.
locked=0
for ((attempt = 0; attempt < 100; attempt++)); do
    if mkdir "$gate" 2>/dev/null; then locked=1; break; fi
    sleep 0.05
done
(( locked == 1 )) || { echo "Node $node_id: process record is busy." >&2; exit 1; }
trap 'rmdir "$gate"' EXIT

if [[ $operation == run ]]; then
    [[ ! -e "$run_dir/cancelled" ]] || exit 1
    "$binary" -c "$config_file" -n "$node_id" -r "$run_id" > "$run_dir/node_${node_id}.log" 2>&1 &
    node_pid=$!
    started=$(ps -p "$node_pid" -o lstart= 2>/dev/null || true)
    printf '%s\n%s\n' "$node_pid" "$started" > "$pid_file"
    rmdir "$gate"
    trap - EXIT
    trap 'kill -TERM "$node_pid" 2>/dev/null || true' HUP INT TERM
    if wait "$node_pid"; then exit 0; else exit "$?"; fi
fi

[[ -r $pid_file ]] || exit 0
{ read -r node_pid; IFS= read -r started; } < "$pid_file"
[[ $node_pid =~ ^[0-9]+$ ]] || { echo "Invalid process record for node $node_id." >&2; exit 1; }
actual=$(ps -ww -p "$node_pid" -o args= 2>/dev/null || true)
[[ -n $actual ]] || exit 0
matches_process() {
    [[ -n $started && $(ps -ww -p "$node_pid" -o args= 2>/dev/null || true) == "$expected" && \
        $(ps -p "$node_pid" -o lstart= 2>/dev/null || true) == "$started" ]]
}
if ! matches_process; then
    echo "Node $node_id: PID $node_pid no longer matches this run; leaving it alone." >&2
    exit 1
fi
kill -TERM "$node_pid" 2>/dev/null || true
for ((attempt = 0; attempt < 20; attempt++)); do
    matches_process || exit 0
    sleep 0.1
done
if matches_process; then kill -KILL "$node_pid" 2>/dev/null || true; fi
