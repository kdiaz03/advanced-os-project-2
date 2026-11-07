#!/usr/bin/env bash
PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)

load_config_path() {
    local input=$1
    if [[ ! -e $input && -e "$PROJECT_DIR/$input" ]]; then input="$PROJECT_DIR/$input"; fi
    CONFIG_FILE="$(cd "$(dirname "$input")" && pwd -P)/$(basename "$input")"
    local key
    key=$(printf '%s' "$CONFIG_FILE" | cksum | awk '{print $1 "-" $2}')
    ACTIVE_DIR="$PROJECT_DIR/logs/active-$key"
}

run_on_host() {
    local host=$1
    shift
    [[ $host =~ ^[A-Za-z0-9][A-Za-z0-9.-]*$ ]] || { echo "Invalid hostname: $host" >&2; return 1; }
    if [[ $host == localhost || $host == 127.0.0.1 ]]; then
        bash "$@"
    else
        local command
        printf -v command '%q ' bash "$@"
        ssh -o BatchMode=yes -o ConnectTimeout=5 -o ServerAliveInterval=5 -o ServerAliveCountMax=2 \
            "$host" "$command"
    fi
}

release_active() {
    local recorded
    [[ -r "$ACTIVE_DIR/run-id" ]] || return 0
    read -r recorded < "$ACTIVE_DIR/run-id"
    [[ $recorded == "$RUN_ID" ]] || return 0
    rm -f "$ACTIVE_DIR/run-id" "$ACTIVE_DIR/config-path" "$ACTIVE_DIR/launcher"
    rmdir "$ACTIVE_DIR"
}
