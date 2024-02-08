import common
import subprocess
import sys

benchmarks = common.benchmarks if len(sys.argv) == 1 else sys.argv[1:]

for b in benchmarks:
    if b not in common.benchmarks:
        print(f"Invalid benchmark {b}, skipping...")

    print(f"Processing benchmark {b}...")

    benchmark_path = f"{common.BENCHMARKS_PATH}/{b}"
    std_path = f"{benchmark_path}/trace.std"
    trace_path = f"{benchmark_path}/trace.txt"
    map_path = f"{benchmark_path}/map.txt"
    grammar_path = f"{benchmark_path}/grammar.txt"

    # java -Xmx16g -classpath "lib/*:ziptrack.jar" PrintTrace -p=../benchmarks/crypt/trace.std -f=std -m=../benchmarks/crypt/map.txt > ../benchmarks/crypt/trace.txt
    p = subprocess.run(["java", "-classpath", f"{common.ZIPTRACK_PATH}/lib/*:{common.ZIPTRACK_PATH}/ziptrack.jar", "PrintTrace", f"-p={std_path}", "-f=std", f"-m={map_path}"], stdout=open(trace_path, "w"), cwd=common.ZIPTRACK_PATH)

    if p.returncode != 0:
        print("Something went wrong")
        sys.exit(1)

    print(f"Trace generated: {trace_path}")
    print(f"Map generated: {map_path}")

    # ./sequitur -d -p -m 2000 < ../benchmarks/crypt/trace.txt > ../benchmarks/crypt/grammar.txt
    p = subprocess.run(["./sequitur", "-d", "-p", "-m", "2000"], stdin=open(trace_path, "r"), stdout=open(grammar_path, "w"), cwd=common.SEQUITUR_PATH)

    if p.returncode != 0:
        print("Something went wrong")
        sys.exit(1)

    print(f"Grammar generated: {grammar_path}")
