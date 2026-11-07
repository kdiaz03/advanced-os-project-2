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

Currently `main` validates configuration and node selection only. The mutex,
application, launcher, cleanup, and checker are added in later commits.
Tests additionally require Python 3. Generated binaries and test fixtures go in
`build/`; run logs will go in `logs/`. Neither directory is committed.

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
