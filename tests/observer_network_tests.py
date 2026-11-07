from pathlib import Path
import os
import socket
import subprocess
import time


run_id = f"observer-negative-{os.getpid()}-{time.time_ns()}"
run_dir = Path("logs") / run_id
run_dir.mkdir(parents=True, exist_ok=False)
processes = []
reserved = []
try:
    for _ in range(2):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(("127.0.0.1", 0))
        reserved.append(sock)
    ports = [sock.getsockname()[1] for sock in reserved]
    config = run_dir / "config.txt"
    config.write_text(f"2 0 0 1\n9 127.0.0.1 {ports[0]}\n4 127.0.0.1 {ports[1]}\n9 4\n9 4\n")
    for sock in reserved:
        sock.close()
    for node in (9, 4):
        proc = subprocess.Popen(
            ["./build/observer_node", str(config), str(node), run_id],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        processes.append((node, proc))
    deadline = time.monotonic() + 12
    results = {}
    for node, proc in processes:
        out, err = proc.communicate(timeout=max(0.1, deadline - time.monotonic()))
        (run_dir / f"node_{node}.log").write_text(out + err)
        results[node] = (proc.returncode, out, err)
    observer_code, log, error = results[9]
    assert observer_code == 1, results
    assert results[4][0] == 0, results
    assert not error, results
    assert "CHECK FAIL overlap node=4 request=1 while node=9 request=1 is active" in log, log
    assert "CHECK FAIL entries=2 exits=2 expected=2" in log, log
    assert "CHECK PASS" not in log, log
    assert log.index("CHECK ENTER node=4") < log.index("CHECK EXIT node=9"), log
    print(f"PASS: deliberate TCP overlap reported failure with complete counts; evidence in {run_dir}")
finally:
    for sock in reserved:
        sock.close()
    for _, proc in processes:
        if proc.poll() is None:
            proc.kill()
        proc.communicate(timeout=3)
