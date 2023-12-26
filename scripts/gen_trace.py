import subprocess
import sys
import util

benchmarks = util.benchmarks if len(sys.argv) == 1 else sys.argv[1:]

for b in benchmarks:
    if b not in util.benchmarks:
        print(f"Invalid benchmark {b}, skipping...")
    print(f"Processing benchmark {b}...")
    benchmark_path = f"{util.BENCHMARKS_PATH}/{b}"
    subprocess.run(["sh", "TEST"], cwd=benchmark_path)
    print(f"Trace generated: {benchmark_path}/full_trace.rr")
