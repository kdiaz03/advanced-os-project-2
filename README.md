# Advanced Operating Systems Project 2

This C++ program runs Maekawa's mutual exclusion algorithm with preemption and checks that no two nodes are ever in their critical sections at the same time. All nodes stop automatically after every node finishes its requests.

## Build and run

On a UTD `csa-dcXX.utdallas.edu` machine, enter the project directory. It must be accessible at the same path on every configured host, with passwordless SSH between hosts. Check that the config file has the correct hostnames and available ports. Hostnames must be full names such as `csa-dc02.utdallas.edu`; short names like `dc02` do not resolve on the cluster. Sample configs are in `configs/`.

Choose a config and run:

```bash
config=configs/cluster-3nodes.txt
./scripts/build.sh "$config"
./scripts/launcher.sh "$config"
```

The build uses `/usr/bin/g++`. The default `c++` on the cluster is a newer GCC that does not match the installed C++ runtime, so avoid building with it.

## Check results

Logs are saved in `logs/<run_id>/`, which the launcher prints at the start of each run. Each node writes `node_<id>.log` with its critical section entries and exits.

The first node in the config also checks the run. Every node reports entering and leaving its critical section to it, and it writes the result to `observer.log`. The launcher prints `CHECK PASS` when every node finishes and no critical sections overlapped.

```bash
tail -n 3 logs/<run_id>/observer.log
```

## Stop an interrupted run

If a run fails or needs to be stopped early:

```bash
./scripts/cleanup.sh "$config"
```

Successful runs require no cleanup.

## Experiments

Each node prints a `METRICS` line at the end of a run with its message count, response times, and run duration. To repeat the experiments in the report and plot the results (requires matplotlib):

```bash
./scripts/experiments.sh cluster 5 40
python3 scripts/analyze.py results/<run>/raw.csv
```

The arguments are the number of runs per setting and the requests per node. Use `local` instead of `cluster` to run every node on one machine.

## Notes

A config has one header line, `n` node lines, and `n` quorum lines. The i-th quorum line belongs to the i-th node, and every ID on it is a quorum member.

The next request's delay starts when the current request is granted. If it runs out during the critical section, the next request starts right after leaving.

Unlike step 2 of the paper, a voter also sends `FAILED` to a waiting request that a newer, higher-priority request overtakes. Without this, a waiting request could hold an `INQUIRE` forever and deadlock.
