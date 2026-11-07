from pathlib import Path
import re
import socket
import subprocess
import time


ROOT = Path.cwd()
TESTS = ROOT / "build" / "run-tests"
TESTS.mkdir(parents=True, exist_ok=True)


# each case gets fresh ports and a generated config under build.
def make_config(name, count, delay=1, cs=2, nodes=3):
    sockets = []
    try:
        for _ in range(nodes):
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.bind(("127.0.0.1", 0))
            sockets.append(sock)
        path = TESTS / (name + ".txt")
        lines = [f"{nodes} {delay} {cs} {count}"]
        lines += [f"{i} 127.0.0.1 {sock.getsockname()[1]}" for i, sock in enumerate(sockets)]
        lines += ["0 1", "1 2", "0 2"] if nodes == 3 else ["0"]
        path.write_text("\n".join(lines) + "\n")
        return path
    finally:
        for sock in sockets:
            sock.close()


def active_run(path):
    for active in (ROOT / "logs").glob("active-*"):
        source = active / "config-path"
        run_id = active / "run-id"
        if source.exists() and run_id.exists() and source.read_text().strip() == str(path):
            return ROOT / "logs" / run_id.read_text().strip()
    return None


def cleanup(path):
    return subprocess.run(["./scripts/cleanup.sh", str(path)], capture_output=True, text=True, timeout=20)


# verify both observer totals and every node's ordered entry/exit records.
def run_case(name, count, delay=1, cs=2, nodes=3):
    path = make_config(name, count, delay, cs, nodes)
    proc = subprocess.Popen(["./scripts/launcher.sh", str(path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        out, err = proc.communicate(timeout=30)
        assert proc.returncode == 0, (name, out, err)
        match = re.search(r"^Logs: (.+)$", out, re.MULTILINE)
        assert match, out
        run_dir = Path(match.group(1))
        summary = f"CHECK PASS entries={nodes * count} exits={nodes * count} expected={nodes * count}"
        assert (run_dir / "observer.log").read_text().splitlines()[-1] == summary
        assert summary in out
        for node in range(nodes):
            log = (run_dir / f"node_{node}.log").read_text()
            enters = re.findall(rf"^CS_ENTER node={node} request=(\d+)$", log, re.MULTILINE)
            exits = re.findall(rf"^CS_EXIT node={node} request=(\d+)$", log, re.MULTILINE)
            expected = list(map(str, range(1, count + 1)))
            assert enters == expected and exits == expected, (name, node, log)
            assert f"Completed {count} requests" in log and "Shutdown complete." in log, log
            assert (run_dir / f"node_{node}.status").read_text().strip() == "0"
        assert active_run(path) is None, "Successful run left an active record"
        assert cleanup(path).returncode == 0
        print(f"PASS: {name}, {nodes * count} completed entries and automatic shutdown")
    finally:
        if proc.poll() is None:
            cleanup(path)
            proc.kill()
            proc.communicate(timeout=5)


# keep an unrelated process alive while exercising cancellation and pid validation.
def interrupted_run():
    path = make_config("interrupted", 100000, 0, 10)
    sentinel = subprocess.Popen(["sleep", "60"])
    proc = subprocess.Popen(["./scripts/launcher.sh", str(path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    run_dir = None
    saved = None
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            run_dir = active_run(path)
            if run_dir and all((run_dir / f"node_{i}.pid").exists() for i in range(3)):
                break
            assert proc.poll() is None, "Launcher exited before process registration"
            time.sleep(0.05)
        else:
            raise AssertionError("Missing process records")
        duplicate = subprocess.run(["./scripts/launcher.sh", str(path)], capture_output=True, text=True, timeout=5)
        assert duplicate.returncode != 0 and "active run" in duplicate.stderr
        record = run_dir / "node_0.pid"
        saved = record.read_text()
        start = subprocess.check_output(["ps", "-p", str(sentinel.pid), "-o", "lstart="], text=True).rstrip("\n")
        # replace a process record temporarily; cleanup must refuse to signal the unrelated process.
        record.write_text(f"{sentinel.pid}\n{start}\n")
        refused = subprocess.run(["./scripts/node.sh", "cleanup", run_dir.name, "0"], capture_output=True, text=True, timeout=5)
        assert refused.returncode != 0 and "leaving it alone" in refused.stderr, refused.stderr
        assert sentinel.poll() is None, "Cleanup stopped an unrelated process"
        record.write_text(saved)
        saved = None
        # cleanup uses the saved run config even if the original config changes.
        path.write_text("changed after launch\n")
        # the saved run records must still work after the source config has changed.
        result = cleanup(path)
        assert result.returncode == 0, result.stderr
        out, err = proc.communicate(timeout=15)
        assert proc.returncode != 0, (out, err)
        assert sentinel.poll() is None
        assert active_run(path) is None
        print("PASS: interrupted-run cleanup, duplicate-run rejection, and unrelated-process protection")
    finally:
        if saved is not None:
            (run_dir / "node_0.pid").write_text(saved)
        if proc.poll() is None:
            cleanup(path)
            proc.kill()
            proc.communicate(timeout=5)
        sentinel.terminate()
        sentinel.wait(timeout=5)


run_case("single", 5, nodes=1)
run_case("normal", 5, delay=10, cs=2)
run_case("contention", 30, delay=0, cs=2)
run_case("zero-requests", 0)
interrupted_run()

# a failed preflight must release the active record even before any jobs exist.
path = make_config("preflight-failure", 1, nodes=1)
path.write_text(path.read_text().replace("127.0.0.1", "-invalid-host"))
result = subprocess.run(["./scripts/launcher.sh", str(path)], capture_output=True, text=True, timeout=10)
assert result.returncode != 0 and "Invalid hostname" in result.stderr, (result.stdout, result.stderr)
assert active_run(path) is None
print("PASS: preflight failure leaves no active run")

# a bind failure must stop the other launched nodes without touching the blocker.
path = make_config("bind-failure", 10)
port = int(path.read_text().splitlines()[1].split()[2])
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as blocker:
    blocker.bind(("0.0.0.0", port))
    blocker.listen()
    result = subprocess.run(["./scripts/launcher.sh", str(path)], capture_output=True, text=True, timeout=20)
    assert result.returncode != 0 and "failed" in result.stderr, (result.stdout, result.stderr)
    assert active_run(path) is None
print("PASS: failed node triggers cleanup of the remaining run")
