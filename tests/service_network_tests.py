from pathlib import Path
import selectors
import socket
import subprocess
import time


def run_case(ids, counts):
    reserved = []
    processes = []
    try:
        for _ in ids:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.bind(("127.0.0.1", 0))
            reserved.append(sock)
        ports = [sock.getsockname()[1] for sock in reserved]
        path = Path("build/service-network.txt")
        lines = [f"{len(ids)} 0 1 15"]
        lines += [f"{node} 127.0.0.1 {port}" for node, port in zip(ids, ports)]
        lines += ["0 1", "1 2", "0 2"] if len(ids) == 3 else [str(ids[0])]
        path.write_text("\n".join(lines) + "\n")
        for sock in reserved:
            sock.close()
        with selectors.DefaultSelector() as selector:
            for node, count in zip(ids, counts):
                proc = subprocess.Popen(
                    ["./build/service_node", str(path), str(node), "service-test", str(count)],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                )
                processes.append(proc)
                selector.register(proc.stdout, selectors.EVENT_READ, (node, count, proc))
            deadline = time.monotonic() + 15
            while selector.get_map():
                remaining = deadline - time.monotonic()
                assert remaining > 0, "Mutex service run timed out"
                events = selector.select(remaining)
                assert events, "Mutex service run timed out"
                for key, _ in events:
                    node, count, proc = key.data
                    line = key.fileobj.readline().strip()
                    assert line == f"COMPLETE {node} {count}", (node, line)
                    selector.unregister(key.fileobj)
        for proc in processes:
            proc.stdin.write("stop\n")
            proc.stdin.flush()
        for proc in processes:
            out, err = proc.communicate(timeout=5)
            assert proc.returncode == 0, (proc.returncode, out, err)
    finally:
        for sock in reserved:
            sock.close()
        for proc in processes:
            if proc.poll() is None:
                proc.kill()
            proc.communicate(timeout=3)


run_case([7], [30])
run_case([0, 1, 2], [1, 15, 15])
print("PASS: localhost service, 30 self-voted entries and 31 distributed entries; early finisher keeps voting")
