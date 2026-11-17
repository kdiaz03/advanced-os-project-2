#!/usr/bin/env bash
# run every experiment setting several times and collect each node's METRICS line.
# usage: ./scripts/experiments.sh <local|cluster> [repeats] [requests_per_node] [sweep=value]
# set RESULTS_DIR to append to an earlier results directory; sweep=value runs one setting (e.g. n=16).
set -euo pipefail
PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)
cd "$PROJECT_DIR"
hosts=${1:-}
repeats=${2:-3}
requests=${3:-50}
only=${4:-}
[[ $hosts == local || $hosts == cluster ]] || { echo "Usage: $0 <local|cluster> [repeats] [requests]" >&2; exit 1; }

# baseline from the handout's example: n=9, d=20 ms, c=10 ms. vary one parameter at a time.
base_n=9; base_d=20; base_c=10
settings=()
for n in 4 9 16 25 36; do settings+=("n $n $base_d $base_c"); done
for d in 5 20 50 100 200 500; do settings+=("d $base_n $d $base_c"); done
for c in 5 10 20 40; do settings+=("c $base_n $base_d $c"); done

out=${RESULTS_DIR:-"results/$(date -u +%Y%m%dT%H%M%SZ)-$hosts"}
mkdir -p "$out/configs"
raw="$out/raw.csv"
[[ -s $raw ]] || echo "sweep,n,d,c,requests,repeat,run_id,node,network_messages,self_messages,response_total_ms,response_max_ms,duration_ms" > "$raw"
./scripts/build.sh configs/local-3nodes.txt > /dev/null

run=0
for setting in "${settings[@]}"; do
    read -r sweep n d c <<< "$setting"
    # a filter such as d=50 selects the sweep and the value of that sweep's parameter.
    if [[ -n $only ]]; then
        declare -A value=([n]=$n [d]=$d [c]=$c)
        [[ $only == "$sweep=${value[$sweep]}" ]] || continue
    fi
    for ((repeat = 1; repeat <= repeats; repeat++)); do
        # rotate base ports so back-to-back runs never reuse a port that is still closing.
        port=$((24200 + (run % 10) * 100))
        run=$((run + 1))
        config="$out/configs/${sweep}-n${n}-d${d}-c${c}-r${repeat}.txt"
        python3 scripts/make_config.py "$n" "$d" "$c" "$requests" --hosts "$hosts" --port "$port" > "$config"
        echo "[$run] sweep=$sweep n=$n d=$d c=$c repeat=$repeat"
        # the launcher also runs the overlap check; any failure stops the experiment.
        if ! launch=$(./scripts/launcher.sh "$config" 2>&1); then
            echo "$launch" >&2
            exit 1
        fi
        run_id=$(sed -n 's/^Run: //p' <<< "$launch")
        for log in "logs/$run_id"/node_*.log; do
            # turn "METRICS node=0 requests=5 ..." into the csv columns after run_id.
            sed -n 's/^METRICS //p' "$log" | awk -v prefix="$sweep,$n,$d,$c,$requests,$repeat,$run_id" '{
                line = prefix
                for (i = 1; i <= NF; i++) { split($i, kv, "="); if (kv[1] != "requests") line = line "," kv[2] }
                print line
            }' >> "$raw"
        done
    done
done
echo "Results: $raw"
