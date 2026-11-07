#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
(( $# == 1 )) || { echo "Usage: $0 <config_file>" >&2; exit 1; }
load_config_path "$1"
[[ -r $CONFIG_FILE && -x "$PROJECT_DIR/build/main" ]] || { echo "Missing config or binary; build first." >&2; exit 1; }
"$PROJECT_DIR/build/main" -c "$CONFIG_FILE" --check-config > /dev/null
mkdir -p "$PROJECT_DIR/logs"
# creating this directory atomically prevents two launchers from claiming the same config.
if ! mkdir "$ACTIVE_DIR" 2>/dev/null; then
    echo "This config already has an active run; use cleanup.sh first." >&2
    exit 1
fi
# give this run its own config copy, process records, and log directory.
RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)-$$-$RANDOM"
run_dir="$PROJECT_DIR/logs/$RUN_ID"
mkdir "$run_dir"
printf '%s\n' "$CONFIG_FILE" > "$ACTIVE_DIR/config-path"
printf '%s\n' "$RUN_ID" > "$ACTIVE_DIR/run-id"
printf '%s\n%s\n%s\n' "$(hostname)" "$$" "$(ps -p $$ -o lstart=)" > "$ACTIVE_DIR/launcher"
jobs=()
launched=0
# on failure, clean up only this run; a preflight failure has no children to stop.
finish() {
    local result=$?
    trap - EXIT INT TERM
    if (( result != 0 && launched == 1 )); then
        if ! "$PROJECT_DIR/scripts/cleanup.sh" "$CONFIG_FILE" "$RUN_ID"; then
            echo "Run failed; retry cleanup from this host. Logs: $run_dir" >&2
            return
        fi
    fi
    for job in "${jobs[@]-}"; do
        if [[ -n $job ]]; then wait "$job" 2>/dev/null || true; fi
    done
    release_active
}
trap finish EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# use the saved config throughout the run even if the original file is edited.
cp "$CONFIG_FILE" "$run_dir/config.txt"
"$PROJECT_DIR/build/main" -c "$run_dir/config.txt" --list-nodes > "$run_dir/nodes.tsv"
echo "Run: $RUN_ID"
echo "Logs: $run_dir"
while IFS=$'\t' read -r node_id host port; do
    # verify the shared binary and config on every host before launching any nodes.
    run_on_host "$host" "$PROJECT_DIR/scripts/node.sh" check "$RUN_ID" "$node_id"
done < "$run_dir/nodes.tsv"
while IFS=$'\t' read -r node_id host port; do
    # start all nodes concurrently because connection setup waits for the other peers.
    launched=1
    (
        if run_on_host "$host" "$PROJECT_DIR/scripts/node.sh" run "$RUN_ID" "$node_id"; then status=0; else status=$?; fi
        printf '%s\n' "$status" > "$run_dir/node_${node_id}.status"
    ) &
    jobs+=("$!")
done < "$run_dir/nodes.tsv"
# watch every status file so one failed node does not hide behind another unfinished node.
while true; do
    complete=1
    while IFS=$'\t' read -r node_id host port; do
        status_file="$run_dir/node_${node_id}.status"
        if [[ ! -s $status_file ]]; then complete=0; continue; fi
        read -r status < "$status_file"
        if [[ $status != 0 ]]; then echo "Node $node_id failed; see $run_dir/node_${node_id}.log" >&2; exit 1; fi
    done < "$run_dir/nodes.tsv"
    if (( complete == 1 )); then break; fi
    sleep 0.1
done
# successful process exits still require the observer's final pass result.
summary=$(tail -n 1 "$run_dir/observer.log")
[[ $summary == "CHECK PASS "* ]] || { echo "Overlap check did not pass; see $run_dir/observer.log" >&2; exit 1; }
echo "$summary"
echo "Run complete: all nodes exited successfully and the overlap check passed."
