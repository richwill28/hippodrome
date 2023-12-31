import common
import subprocess
import sys

benchmarks = common.benchmarks if len(sys.argv) == 1 else sys.argv[1:]

for b in benchmarks:
    if b not in common.benchmarks:
        print(f"Invalid benchmark {b}, skipping...")

    print(f"Processing benchmark {b}...")

    benchmark_path = f"{common.BENCHMARKS_PATH}/{b}"

    p = subprocess.run(["sh", "TEST"], cwd=benchmark_path)

    if p.returncode != 0:
        print("Something went wrong")
        sys.exit(1)

    print(f"Trace generated: {benchmark_path}/full_trace.rr")
