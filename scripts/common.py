#!/usr/bin/python3

import os
import subprocess

HOME_PATH = "/home/richwill/repo/hippodrome"

RAPID_PATH = f"{HOME_PATH}/rapid"
RAPID_CLASSPATH = f"{RAPID_PATH}/rapid.jar:{RAPID_PATH}/lib/*:{RAPID_PATH}/lib/jgrapht/*"
BENCHMARKS_PATH = f"{HOME_PATH}/benchmarks"
ATOM_SPECS_PATH = f"{HOME_PATH}/atomicity_specs"
ZIPTRACK_PATH = f"{HOME_PATH}/ziptrack"
SEQUITUR_PATH = f"{HOME_PATH}/sequitur/c++"

benchmarks = sorted([b for b in os.listdir(BENCHMARKS_PATH) if os.path.isdir(f"{BENCHMARKS_PATH}/{b}")])
