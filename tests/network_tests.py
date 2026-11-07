from pathlib import Path
import socket
import struct
import subprocess
import time


ROOT = Path("build/network-tests")
ROOT.mkdir(parents=True, exist_ok=True)


def listener():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    sock.listen()
    sock.settimeout(5)
    return sock


def config(name, ports, ids=None):
    ids = list(range(len(ports))) if ids is None else ids
    path = ROOT / (name + ".txt")
    lines = [f"{len(ports)} 20 10 5"]
    lines += [f"{node} 127.0.0.1 {port}" for node, port in zip(ids, ports)]
    # directed cyclic membership exercises the full mesh independently of quorums.
    if len(ids) == 3:
        lines += [f"{ids[0]} {ids[1]}", f"{ids[1]} {ids[2]}", f"{ids[0]} {ids[2]}"]
    else:
        lines += [" ".join(map(str, ids))] * len(ids)
    path.write_text("\n".join(lines) + "\n")
    return path


def start(path, node, run="test", startup=2000, io=500, count=128):
    return subprocess.Popen(
        ["./build/network_node", str(path), str(node), run, str(startup), str(io), str(count)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )


def finish(proc, success=True, diagnostic=""):
    out, err = proc.communicate(timeout=12)
    assert (proc.returncode == 0) == success, (proc.args, proc.returncode, out, err)
    assert diagnostic in out + err, (diagnostic, out, err)


def dispose(proc):
    if proc is not None and proc.poll() is None:
        proc.kill()
        proc.communicate(timeout=3)


def mesh(ids):
    reserved = [listener() for _ in ids]
    ports = [sock.getsockname()[1] for sock in reserved]
    path = config("mesh-" + str(len(ids)), ports, ids)
    for sock in reserved:
        sock.close()
    processes = []
    try:
        # start in reverse order to exercise listening before lower ids connect.
        for node in reversed(ids):
            processes.append(start(path, node))
        for proc in processes:
            finish(proc, diagnostic="ordered messages")
    finally:
        for proc in processes:
            dispose(proc)


def read_exact(conn, size):
    result = b""
    while len(result) < size:
        chunk = conn.recv(size - len(result))
        assert chunk, "Unexpected EOF in test peer"
        result += chunk
    return result


def frame(sequence, kind=1, sender=1, magic=0x50324D31):
    return struct.pack("!IIIIQ", magic, kind, sender, sender, sequence)


def fake_peer(case, diagnostic=""):
    proc = None
    with listener() as server, listener() as reserved:
        path = config(case, [reserved.getsockname()[1], server.getsockname()[1]])
        reserved.close()
        count = 1000000 if case == "stalled-write" else 4
        try:
            proc = start(path, 0, count=count, io=300)
            with server.accept()[0] as conn:
                conn.settimeout(5)
                sender, size = struct.unpack("!II", read_exact(conn, 8))
                assert sender == 0 and size < 1024 * 1024
                greeting = read_exact(conn, size)
                if case == "handshake-size":
                    conn.sendall(struct.pack("!II", 1, 1024 * 1024 + 1))
                elif case == "stalled-handshake":
                    conn.sendall(b"\0")
                else:
                    if case == "wrong-run":
                        greeting = greeting.replace(b"test\n", b"different\n", 1)
                    elif case == "wrong-config":
                        greeting = greeting.replace(b"20 10 5", b"20 10 6", 1)
                    peer_id = 9 if case == "wrong-peer" else 1
                    conn.sendall(struct.pack("!II", peer_id, len(greeting)) + greeting)
                    if case not in {"wrong-run", "wrong-config", "wrong-peer", "stalled-write"}:
                        read_exact(conn, (count + 1) * 24)
                        if case == "fragmented-and-combined":
                            first = frame(0)
                            conn.sendall(first[:3])
                            time.sleep(0.02)
                            conn.sendall(first[3:])
                            conn.sendall(b"".join(frame(i) for i in range(1, count)) + frame(count, kind=2))
                        elif case == "wrong-sender":
                            conn.sendall(frame(0, sender=9))
                        elif case == "unknown-type":
                            conn.sendall(frame(0, kind=99))
                        elif case == "wrong-version":
                            conn.sendall(frame(0, magic=0))
                        elif case == "truncated-frame":
                            conn.sendall(frame(0)[:7])
                            conn.shutdown(socket.SHUT_WR)
                        elif case == "stalled-frame":
                            conn.sendall(frame(0)[:1])
                        elif case == "disconnect":
                            conn.shutdown(socket.SHUT_RDWR)
                finish(proc, success=case == "fragmented-and-combined", diagnostic=diagnostic)
        finally:
            dispose(proc)


mesh([7])
mesh([9, 4, 12])
print("PASS: single node and three-node full mesh, asymmetric quorums, 128 messages per peer")

reserved = [listener(), listener()]
missing = config("missing-peer", [sock.getsockname()[1] for sock in reserved])
for sock in reserved:
    sock.close()
for node in (0, 1):
    proc = start(missing, node, startup=300)
    try:
        finish(proc, success=False, diagnostic="timed out")
    finally:
        dispose(proc)
print("PASS: missing outgoing and incoming peers time out")

for case, diagnostic in [
    ("fragmented-and-combined", "PASS"),
    ("wrong-run", "run/config mismatch"),
    ("wrong-config", "run/config mismatch"),
    ("wrong-peer", "peer ID"),
    ("handshake-size", "handshake size"),
    ("stalled-handshake", "timed out"),
    ("wrong-sender", "sender ID mismatch"),
    ("unknown-type", "Unknown message type"),
    ("wrong-version", "message version"),
    ("truncated-frame", "closed connection"),
    ("stalled-frame", "timed out"),
    ("disconnect", "closed connection"),
    ("stalled-write", "timed out"),
]:
    fake_peer(case, diagnostic)
    print("PASS:", case)
