# Advanced Operating Systems Project 2

C++17 and POSIX TCP. The planned service uses one protocol worker per node,
a full TCP mesh, and the supplied Maekawa quorums. Deliverable 2 adds a TCP
observer. Performance experiments and the second algorithm are outside this stage.

## Build and check

From this directory, with a C++17 compiler (override with `CXX` if needed):

```bash
config=configs/local-3nodes.txt
./scripts/build.sh "$config"
./build/main -c "$config" -n 0
./scripts/test.sh
```

Currently `main` validates configuration and node selection only. The transport
is exercised by `build/network_node`, compiled by `scripts/test.sh`. The mutex,
application, launcher, cleanup, and checker are added in later commits.
Tests additionally require Python 3. Generated binaries and test fixtures go in
`build/`; run logs will go in `logs/`. Neither directory is committed.

## Transport

Every pair has one bidirectional TCP connection; the lower node ID connects.
Listeners open before connection attempts. Quorum membership does not restrict
connections. A two-way handshake checks peer IDs, wire version, run ID, and the
parsed configuration, including quorum-row order. Use one unique run ID across
all nodes of a run. Configuration whitespace and comments do not affect it.

The transport currently uses IPv4 to keep listener/address handling small;
configured hosts must resolve to IPv4. The required cluster still needs testing.
Connection setup, handshakes, writes, and started frames have deadlines. Idle
connections can wait indefinitely. System hostname resolution is synchronous
and is not interrupted by the socket deadline. There is no reconnect/replay.

Frames are fixed at 24 bytes with explicit big-endian encoding. Each peer has a
receiver thread and a write mutex. Receivers invoke callbacks in per-peer FIFO
order. Future service callbacks will enqueue events for one protocol worker,
which alone changes Maekawa state and orders its outgoing messages. Self-voting
will use local events; sending to oneself through `Network` is an error.

Read errors reach an error callback; send/setup errors throw. The service must
fail the run and wake blocked callers on either path. Stop producers before
calling `stop()` from the owning thread, and keep callback targets alive until
it returns. Error callbacks must not throw. The transport does not decide global
completion; the application's voting/drain lifecycle comes in a later commit.

The tests exchange 128 ordered messages per peer in a three-process mesh and
check a single-node setup, fragmented/combined frames, invalid messages and
handshakes, missing peers, disconnects, and stalled reads/writes. They use only
localhost, temporary ports, and their own child processes. They are transport
tests, not mutual-exclusion tests.

## Configuration interpretations

- Read four header fields, then `n` node records and `n` quorum rows: `1+2n`
  valid lines. Confirm the handout's contradictory `n+1` statement with the TA.
- Quorum rows correspond to node-record order. All row tokens are member IDs;
  the first token is not an owner. IDs need not be contiguous. Confirm row
  mapping with the TA, particularly when node records are out of ID order.
- Ignore lines whose first whitespace-separated token is not an unsigned
  decimal integer, and strip inline `#` comments. Inputs are at most 100,000 bytes.
- Timing means are finite nonnegative milliseconds (fractions accepted).
  Zero will mean no delay. Request counts may be zero.
- Reject unknown/duplicate quorum members and disjoint quorums. Do not require
  equal sizes, symmetric membership, or add self-membership automatically.
- Resolve hostnames exactly as supplied. The assignment's `dcXX` aliases need
  confirmation on the cluster; do not silently rewrite them.

Planned timing: sample the next delay at the current grant and issue the next
request at the later of that deadline or completion of `cs_leave()`. The first
request starts immediately after startup. Confirm the case where the delay
expires during the CS with the TA.

## Validation status

Local checks do not validate the required `csa-dcXX.utdallas.edu` demo. Verify
cluster DNS, SSH, compiler/runtime, paths, and cross-host TCP before claiming
cluster support. No Project 1 runtime paths or hostname aliases are assumed.
