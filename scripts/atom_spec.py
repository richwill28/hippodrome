import subprocess
import sys
import util

benchmarks = util.benchmarks if len(sys.argv) == 1 else sys.argv[1:]

for b in benchmarks:
    if b not in util.benchmarks:
        print(f"Invalid benchmark {b}, skipping...")
    print(f"Processing benchmark {b}...")
    benchmark_path = f"{util.BENCHMARKS_PATH}/{b}"
    trace_path = f"{benchmark_path}/full_trace.rr"
    atom_spec_path = f"{util.ATOM_SPEC_PATH}/{b}.txt"
    output_trace_path = f"{benchmark_path}/trace.std"
    subprocess.run(["java", "-classpath", util.RAPID_CLASSPATH, "ExcludeMethods", "-f=rr", f"-p={trace_path}", f"-m={atom_spec_path}"], stdout=open(output_trace_path, "w"), cwd=benchmark_path)
    print(f"Trace generated: {output_trace_path}")
