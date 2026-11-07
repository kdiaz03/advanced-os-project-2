import subprocess

# validation mode exercises argument handling without starting network listeners.
base = ["./build/main", "--check-config"]
valid = ["-c", "configs/local-3nodes.txt", "-n", "0"]
# cover valid input along with missing, repeated, trailing, and malformed arguments.
cases = [
    (valid, True),
    ([], False),
    (["-c"], False),
    (valid + ["extra"], False),
    (valid + ["-n", "1"], False),
    (valid[:-1] + ["-1"], False),
    (valid[:-1] + ["0junk"], False),
    (valid[:-1] + ["3"], False),
    (["-c", "build/no-such-config", "-n", "0"], False),
]
for args, should_pass in cases:
    result = subprocess.run(base + args, capture_output=True, text=True, timeout=5)
    assert (result.returncode == 0) == should_pass, (args, result.stdout, result.stderr)
print("PASS: CLI tests")
