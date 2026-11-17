#!/usr/bin/env python3
"""write an experiment config with grid quorums, matching the handout's 9-node example."""
import argparse
import math
import sys


# node i sits at (row, column) of a k x k grid; its quorum is its row plus its column.
def grid_quorum(node, k):
    row, column = divmod(node, k)
    members = {row * k + j for j in range(k)} | {i * k + column for i in range(k)}
    return sorted(members)


def host_for(node, hosts, machines):
    if hosts == "local":
        return "localhost"
    # spread nodes round-robin over csa-dc01 .. csa-dcMM.
    return f"csa-dc{node % machines + 1:02d}.utdallas.edu"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("n", type=int, help="node count; must be a perfect square")
    parser.add_argument("d", type=float, help="mean inter-request delay in ms")
    parser.add_argument("c", type=float, help="mean cs execution time in ms")
    parser.add_argument("requests", type=int, help="requests per node")
    parser.add_argument("--hosts", choices=["local", "cluster"], default="local")
    parser.add_argument("--machines", type=int, default=45, help="cluster machines to use")
    parser.add_argument("--port", type=int, default=24200, help="port of node 0")
    args = parser.parse_args()

    k = math.isqrt(args.n)
    if k * k != args.n or k == 0:
        sys.exit("grid quorums need a perfect-square node count")
    machines = max(1, min(args.machines, 45))

    # numbers print without a trailing .0 so the header matches the handout's format.
    print(f"{args.n} {args.d:g} {args.c:g} {args.requests}")
    print("# node id, host, port")
    for node in range(args.n):
        print(f"{node} {host_for(node, args.hosts, machines)} {args.port + node}")
    print("# grid quorums in node order: row plus column")
    for node in range(args.n):
        print(" ".join(str(member) for member in grid_quorum(node, k)))


if __name__ == "__main__":
    main()
