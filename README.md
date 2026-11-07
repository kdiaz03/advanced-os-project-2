# Advanced Operating Systems Project 2 — Deliverables 1 and 2

This C++17 program runs the preemptive Maekawa mutual exclusion service.
Each node generates the configured number of critical-section requests and
continues serving votes until all nodes finish. A TCP observer checks for overlapping
critical sections. All nodes then stop automatically.

## Build and run

From the project directory, choose a config and run:

```bash
config=configs/local-3nodes.txt
./scripts/build.sh "$config"
./scripts/launcher.sh "$config"
```

Use `configs/cluster-3nodes.txt` for the UTD machines, after checking the hostnames
and available ports. Build on the cluster before launching there. The project
must be accessible at the same path on every host, with passwordless SSH between
hosts. The launcher checks access to the binary and saved config before starting.
Use `CXX=g++ ./scripts/build.sh "$config"` to select a compiler if needed.

The launcher waits for the run to finish and reports whether every node exited
successfully. Run one instance per config at a time. For manual testing, start
one process per configured node in separate terminals:

```bash
./build/main -c "$config" -n 0
```

## Check results

The launcher prints the run's log directory: `logs/<run-id>/`. Each node writes
`node_<id>.log`, including its entries, exits, completed request count, and
`Shutdown complete.` message. Binaries are saved in `build/`.

The first configured node hosts the observer using the existing TCP connections.
It writes entries, exits, and the final result to `observer.log`. The launcher
prints `CHECK PASS` with the expected entry/exit totals only after all nodes exit
successfully. Overlap, unmatched events, or incomplete counts fail the run.

Each node confirms entry before its CS body and confirms exit before releasing
its votes. The observer acknowledges overlapping entries immediately and records
failure; it never waits for another node to leave. It checks the instrumented CS
body without comparing clocks across machines. Passing tests are evidence, not
a proof of correctness for every possible execution.

Run the local tests with Python 3 installed:

```bash
./scripts/test.sh
```

These test the protocol, observer, request counts, shutdown, and cleanup. Normal
and contention runs must finish within 30 seconds with exact completion counts.
A separate two-process test deliberately overlaps intervals and must report
`CHECK FAIL` within 12 seconds; the test script reports PASS when that expected
failure is detected. Its evidence is saved in `logs/observer-negative-<id>/`.
Only that test executable bypasses the mutex; the application always uses it.

Compilation and a multi-host run on `csa-dcXX.utdallas.edu` still need
verification before the required cluster demo.

## Stop an interrupted run

From another terminal on the launch host, run:

```bash
./scripts/cleanup.sh "$config"
```

Cleanup uses saved process records and stops only matching processes from that
run. It keeps the logs. Successful runs require no cleanup.

## Config notes

The parser reads four header fields, `n` node records, and `n` quorum rows.
Quorum rows follow node-record order; every number in a row is a member ID.
Whitespace, inline `#` comments, and other nonnumeric lines are supported.

Delays are exponential, in milliseconds. The next delay starts at the current
grant; if it expires during the critical section, the next request starts after
leaving. Zero means no delay; the first request starts immediately.

Confirm the handout's `n+1` versus `1+2n` line count, quorum-row ordering, and
this delay interpretation with the TA. The sample's `dcXX` aliases also need
confirmation on the cluster. Hostnames are used unchanged and must resolve to IPv4.
