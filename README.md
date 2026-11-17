# CS/CE 6378 Project 2 — Maekawa Mutual Exclusion

Keven Diaz (kxd210034). C++17 implementation of Maekawa's algorithm with
preemption, an automated overlap check, and the performance experiments. The
experiment results are in the separate PDF report.

## Build and run

```bash
config=configs/cluster-3nodes.txt      # or configs/local-3nodes.txt
./scripts/build.sh "$config"
./scripts/launcher.sh "$config"
```

Build on the cluster before launching there. Every host needs the project at the
same path and passwordless SSH from the launch host. Use `CXX=g++` before
`build.sh` to pick a compiler. To run one node by hand:
`./build/main -c "$config" -n <node_id>`.

## Results and correctness check

The launcher prints the run's log directory, `logs/<run-id>/`, with one
`node_<id>.log` per node. The first configured node also runs an observer: each
node reports entering and leaving its critical section and waits for an
acknowledgement, and the observer fails the run if two nodes are ever inside at
once or if entry/exit counts are wrong. Its result is in `observer.log`, and
the launcher prints `CHECK PASS` only if every node exited cleanly and the check
passed.

Each node also prints a `METRICS` line: protocol messages sent (network and
self), total and maximum response time, and its active duration.

## Stop an interrupted run

```bash
./scripts/cleanup.sh "$config"
```

## Experiments

```bash
./scripts/experiments.sh local 5 40      # or: cluster; args are repeats and requests per node
python3 scripts/analyze.py results/<run>/raw.csv
```

`experiments.sh` generates grid-quorum configs with `make_config.py`, varies
n, d and c one at a time around n=9, d=20, c=10, and runs each through the
launcher. `analyze.py` (needs matplotlib) averages the runs and plots messages
per CS, response time and throughput.

## Notes

- The parser expects one header line, n node lines and n quorum lines; the i-th
  quorum line belongs to the i-th node, and every ID on it is a member.
- The inter-request delay starts when a request is granted. If it runs out
  during the critical section, the next request starts right after leaving.
- Unlike the paper's step 2, a voter also sends FAILED to a waiting request
  that a newer, higher-priority request overtakes. Without this a waiting
  request could hold onto an INQUIRE forever and deadlock.
