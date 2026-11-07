#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
(( $# == 1 || $# == 2 )) || { echo "Usage: $0 <config_file>" >&2; exit 1; }
load_config_path "$1"
[[ -r "$ACTIVE_DIR/run-id" ]] || { echo "No active run for this config."; exit 0; }
[[ $(cat "$ACTIVE_DIR/config-path") == "$CONFIG_FILE" ]] || { echo "Config record mismatch." >&2; exit 1; }
read -r RUN_ID < "$ACTIVE_DIR/run-id"
if (( $# == 2 )) && [[ $RUN_ID != "$2" ]]; then exit 0; fi
[[ $RUN_ID =~ ^[A-Za-z0-9._-]+$ && $RUN_ID != .* ]] || exit 1
run_dir="$PROJECT_DIR/logs/$RUN_ID"
touch "$run_dir/cancelled"
failed=0
while IFS=$'\t' read -r node_id host port; do
    if ! run_on_host "$host" "$PROJECT_DIR/scripts/node.sh" cleanup "$RUN_ID" "$node_id"; then failed=1; fi
done < "$run_dir/nodes.tsv"
(( failed == 0 )) || { echo "Cleanup incomplete; records kept in $run_dir." >&2; exit 1; }

# the running launcher removes its own active record after its children exit.
if [[ -r "$ACTIVE_DIR/launcher" ]]; then
    { read -r launch_host; read -r launch_pid; IFS= read -r launch_start; } < "$ACTIVE_DIR/launcher"
    if [[ $launch_host == "$(hostname)" && $launch_pid =~ ^[0-9]+$ && \
        $(ps -p "$launch_pid" -o lstart= 2>/dev/null || true) != "$launch_start" ]]; then release_active; fi
fi
echo "Cleanup finished for run $RUN_ID."
