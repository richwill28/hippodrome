import common
import subprocess
import sys

benchmarks = common.benchmarks if len(sys.argv) == 1 else sys.argv[1:]

for b in benchmarks:
    if b not in common.benchmarks:
        print(f"Invalid benchmark {b}, skipping...")

    print(f"Processing benchmark {b}...")

    benchmark_path = f"{common.BENCHMARKS_PATH}/{b}"
    trace_path = f"{benchmark_path}/full_trace.rr"
    atom_spec_path = f"{common.ATOM_SPECS_PATH}/{b}.txt"
    std_path = f"{benchmark_path}/trace.std"
    
    p = subprocess.run(["java", "-classpath", common.RAPID_CLASSPATH, "ExcludeMethods", "-f=rr", f"-p={trace_path}", f"-m={atom_spec_path}"], stdout=open(std_path, "w"), cwd=benchmark_path)

    if p.returncode != 0:
        print("Something went wrong")
        sys.exit(1)

    print(f"Trace generated: {std_path}")
