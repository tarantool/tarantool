#!/usr/bin/env python3
"""Run TLC on generated runs, one at a time, stopping at the first failure.

Usage:
    run.py --tlc tla2tools.jar --metadir DIR
           [--java "<java and its options>"] [--java-heap SIZE] [--fpmem X]
           [--workers N|auto] [--checkpoint N] [--interval N]
           [--simulate [--depth D] [--traces N]] MODULE.tla...

A run is a module gen.py generated and the config beside it, <name>.tla
and <name>.cfg. Above its module line the module carries a YAML header
with what this script needs, which the TLA+ tools ignore:
    library: the dirs of the modules it extends, relative to the file,
             passed to the JVM as -DTLA-Library
    witness: the check a reachability witness must end violating
    simulate: {traces: N, depth: D} of a simulation
Arguments which are not .tla files are skipped, so a glob over a directory
holding the outputs too is fine.

A run is checked exhaustively, or by TLC's random simulation: random
behaviors from the initial state, each up to a depth (TLC's default 100),
the invariants and properties checked along the way, nothing remembered
between behaviors. A violation found this way is as real as in the
exhaustive run and comes with its trace; a clean end proves nothing, and is
reported as such. Deep scenarios the exhaustive search can't reach are
found this way, if they have many paths. A run is simulated when its header
says so: N behaviors in total, depth optional. --simulate overrides: every
run is simulated, with --traces N in total (none: until a violation or a
kill) and --depth D.

The command run for each module is
    <java> [-Xmx<heap>] -DTLA-Library=<dirs> -cp <jar> tlc2.TLC -workers W
           [-noGenerateSpecTE] [-fpmem X] [-checkpoint N]
           [-simulate [num=N] [-depth D]] -metadir DIR/<name>
           -config <name>.cfg <name>.tla
in the module's directory. -noGenerateSpecTE is passed when the jar knows it
(TLC 2.20 writes a trace exploration spec next to the module on a failure
unless told not to; 2.19 writes it only on request and rejects the flag): the
spec is never used, the trace is in the _out.txt. The metadir is where TLC
keeps its run files - the disk state queue, the fingerprint spills, the
checkpoints. TLC never says which dir it used, so the script chooses:
DIR/<name>. After an expected outcome that dir is deleted; after an
unexpected one it is kept and its path printed - a checkpoint in it is what
TLC's -recover resumes from. --java is the JVM binary with its own options
as one string, "java" by default, for example
    --java "java -XX:+UseParallelGC -Dtlc2.tool.queue.IStateQueue=MemStateQueue"
Aliases are not visible to a subprocess, so pass the expanded command. The
JVM options must precede the jar, TLC's own options follow it; the script
puts each in its place. --java-heap, --fpmem and --checkpoint are passed only
when given, otherwise the JVM's and TLC's defaults apply - see --help for
what the defaults are and why they matter on a big run.

A run without a witness must pass. A witness run must end with TLC
violating the invariant or action property its header names, the negation
of a scenario: the trace is the scenario reached. A pass means the scenario
is unreachable; another invariant, a failed Assert, a deadlock or a TLC
error are failures like anywhere else. Either way the run stops at the
first unexpected outcome.

The complete TLC output of every run goes next to its module, as
<name>_out.txt, with a footer giving the command and the wall time - TLC
itself reports the state counts and the depth at the end. Every N seconds
(default 60) the last TLC progress line is checked, and printed with the
module's name and position when it differs from the last printed one.
"""
import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

import yaml

PASS_MARK = "Model checking completed. No error has been found."
ERROR_RE = re.compile(r"^Error:", re.MULTILINE)
VIOLATION_RE = re.compile(
    r"^Error: (?:Invariant|Action property) (\S+) is violated\.", re.MULTILINE)
HEADER_KEYS = {"library", "witness", "simulate"}


def last_progress(path):
    try:
        with open(path) as f:
            lines = f.readlines()
    except OSError:
        return ""
    for line in reversed(lines):
        # "Progress(depth) ..." in the exhaustive search, "Progress: ..." in a
        # simulation.
        if line.startswith("Progress") or \
                line.startswith("Computing initial states"):
            return line.strip()
    return lines[-1].strip() if lines else ""


def is_module_line(line):
    """Whether the line is the `---- MODULE name ----` one."""
    return line.startswith("----") and "MODULE" in line.strip("- ").split()


def read_header(module_path):
    """The YAML header above the module line: the library dirs, the check of
    a witness, the simulation options."""
    with open(module_path) as f:
        lines = []
        for line in f:
            if is_module_line(line):
                break
            lines.append(line)
    header = yaml.safe_load("".join(lines)) or {}
    if not isinstance(header, dict) or not set(header) <= HEADER_KEYS or \
            not isinstance(header.get("library"), list):
        sys.exit("%s: the header above the module line is library (a list), "
                 "witness and simulate" % module_path)
    sim = header.get("simulate")
    if sim is not None and \
            ("traces" not in sim or set(sim) - {"traces", "depth"}):
        sys.exit("%s: simulate takes traces (required) and depth" % module_path)
    return header


def simulate_opts(sim, workers):
    """TLC's options for a simulation: sim["traces"] behaviors in total (none:
    unbounded), each up to sim["depth"] steps (none: TLC's default)."""
    opts = ["-simulate"]
    if sim.get("traces") is not None:
        # TLC's num is per worker.
        opts.append("num=%d" % -(-sim["traces"] // workers))
    if sim.get("depth") is not None:
        opts += ["-depth", str(sim["depth"])]
    return opts


def run_one(cmd, cwd, out_path, interval, label, simulate):
    started = time.time()
    reported = None
    with open(out_path, "w") as out:
        proc = subprocess.Popen(cmd, cwd=cwd, stdout=out,
                                stderr=subprocess.STDOUT)
        next_report = started + interval
        while proc.poll() is None:
            time.sleep(1)
            if time.time() < next_report:
                continue
            next_report += interval
            line = last_progress(out_path)
            if line == reported:
                continue
            reported = line
            print("%s %s" % (label, line), flush=True)
    elapsed = time.time() - started
    with open(out_path) as f:
        text = f.read()
    # A simulation has no completion mark; it passed when TLC ended by itself
    # without an error.
    passed = proc.returncode == 0 and \
        (PASS_MARK in text or (simulate and not ERROR_RE.search(text)))
    with open(out_path, "a") as out:
        out.write("\n=== run.py: %s\n=== run.py: %s in %d seconds, "
                  "exit code %d\n"
                  % (" ".join(cmd), "PASS" if passed else "FAIL", elapsed,
                     proc.returncode))
    return passed, elapsed, text


def print_failure(text, out_path):
    # The errors, and the messages of the failed Asserts: a string, or a
    # tuple when the message carries a value.
    for line in text.splitlines():
        if line.startswith("Error:") or line.startswith("\"") or \
                line.startswith("<<"):
            print("  " + line)
    print("  output: %s" % out_path)
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "tlatrace.py")
    if os.path.exists(tool) and "State 1:" in text:
        # The tool writes to the same stdout: flush, or its lines come first.
        print("  trace actions:", flush=True)
        subprocess.run([sys.executable, tool, out_path, "actions"])


def verdict(passed, text, witness, simulate):
    """The outcome against the expectation: (is_expected, description).
    witness is the check a witness run must end violating, None for a plain
    run."""
    if witness is None:
        if passed and simulate:
            return True, "PASS, the simulation found nothing"
        return passed, "PASS" if passed else "FAIL"
    if passed and simulate:
        return False, "NOT FOUND, the simulation never violated %s" % witness
    if passed:
        return False, "NOT REACHED, the model never violates %s" % witness
    violated = VIOLATION_RE.findall(text)
    if violated == [witness]:
        return True, "REACHED, %s is violated" % witness
    if violated:
        return False, "FAIL, %s is violated instead of %s" % \
            (", ".join(violated), witness)
    return False, "FAIL, not by an invariant or property"


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tlc", required=True, metavar="JAR",
                        help="path to tla2tools.jar")
    parser.add_argument("--java", default="java", metavar="CMD",
                        help="the JVM binary with its options, as one string; "
                        "default 'java'. Example: 'java -XX:+UseParallelGC "
                        "-Dtlc2.tool.queue.IStateQueue=MemStateQueue' - the "
                        "latter keeps the state queue in memory instead of "
                        "disk pool files")
    parser.add_argument("--java-heap", metavar="SIZE",
                        help="the JVM heap, passed as -Xmx: e.g. 48g. All of "
                        "TLC lives in it - the fingerprint set, the state "
                        "queue, the states the workers build. Without it the "
                        "JVM takes a quarter of the machine's RAM, whatever "
                        "is free")
    parser.add_argument("--fpmem", metavar="X",
                        help="TLC -fpmem: the heap share of the fingerprint "
                        "set (one 8-byte fingerprint per distinct state): a "
                        "fraction of the heap, or MB if above 1. TLC's "
                        "default is 0.25. The rest of the heap is for the "
                        "workers and the queue; 0.5 of a 48g heap holds 3e9 "
                        "states and leaves 24g")
    parser.add_argument("--workers", default="1",
                        help="TLC worker count, default 1; 'auto' is the core "
                        "count")
    parser.add_argument("--checkpoint", metavar="MIN",
                        help="TLC -checkpoint: minutes between checkpoints, "
                        "written to the metadir; a dead run resumes from one "
                        "with TLC's -recover <metadir>/<id>")
    parser.add_argument("--metadir", required=True, metavar="DIR",
                        help="where TLC keeps its run files - the disk state "
                        "queue, fingerprint spills, checkpoints: DIR/<name>, "
                        "passed as TLC's -metadir. Deleted after an "
                        "expected outcome, kept and printed after an "
                        "unexpected one")
    parser.add_argument("--interval", type=int, default=60,
                        help="progress report period, seconds")
    parser.add_argument("--simulate", action="store_true",
                        help="TLC -simulate on every run, whatever its header "
                        "says: random behaviors instead of the exhaustive "
                        "search, until a violation or until killed; a clean "
                        "end proves nothing and is reported as such")
    parser.add_argument("--depth", metavar="D", type=int,
                        help="with --simulate, TLC -depth: the length of a "
                        "simulated behavior, TLC's default 100")
    parser.add_argument("--traces", metavar="N", type=int,
                        help="with --simulate, stop after N behaviors in total "
                        "(TLC counts per worker; N is divided by the worker "
                        "count, rounded up)")
    parser.add_argument("modules", nargs="+",
                        help="the generated .tla files to run, in order")
    args = parser.parse_args()
    if not args.simulate and \
            (args.traces is not None or args.depth is not None):
        parser.error("--traces and --depth need --simulate; a run's own "
                     "simulation is set in the matrix")

    modules = [os.path.abspath(m) for m in args.modules if m.endswith(".tla")]
    if not modules:
        print("no .tla files given")
        return 2
    jvm = shlex.split(args.java)
    if args.java_heap is not None:
        jvm.append("-Xmx" + args.java_heap)
    tlc = ["-cp", os.path.abspath(args.tlc), "tlc2.TLC"]
    tlc_opts = ["-workers", args.workers]
    # The trace exploration spec TLC writes next to the module on a failure
    # is never used: the _out.txt holds the trace, tlatrace.py reads it. TLC
    # 2.20 writes it by default and has the flag to not; 2.19 writes it only
    # on request and rejects the flag. Ask the jar.
    tlc_help = subprocess.run(jvm + tlc + ["-h"], capture_output=True,
                              text=True).stdout
    if "-noGenerateSpecTE" in tlc_help:
        tlc_opts.append("-noGenerateSpecTE")
    if args.fpmem is not None:
        tlc_opts += ["-fpmem", args.fpmem]
    if args.checkpoint is not None:
        tlc_opts += ["-checkpoint", args.checkpoint]
    # "auto" is the core count, as in TLC.
    workers = os.cpu_count() if args.workers == "auto" else int(args.workers)
    metadir = os.path.abspath(args.metadir)
    total_started = time.time()
    for i, module in enumerate(modules, 1):
        cwd = os.path.dirname(module)
        name = os.path.basename(module)[:-len(".tla")]
        cfg = name + ".cfg"
        if not os.path.exists(os.path.join(cwd, cfg)):
            print("%s has no %s beside it" % (module, cfg))
            return 2
        out_path = os.path.join(cwd, name + "_out.txt")
        meta = os.path.join(metadir, name)
        header = read_header(module)
        library = [os.path.normpath(os.path.join(cwd, d))
                   for d in header["library"]]
        if args.simulate:
            sim = {"traces": args.traces, "depth": args.depth}
        else:
            sim = header.get("simulate")
        cmd = jvm + ["-DTLA-Library=" + os.pathsep.join(library)] + tlc + \
            tlc_opts
        if sim is not None:
            cmd = cmd + simulate_opts(sim, workers)
        cmd = cmd + ["-metadir", meta, "-config", cfg, name + ".tla"]
        label = "[%d/%d] %s" % (i, len(modules), name)
        mode = ""
        if sim is not None:
            mode = ", simulation of %s traces" % \
                ("unbounded" if sim.get("traces") is None else sim["traces"])
        print("%s START%s" % (label, mode), flush=True)
        passed, elapsed, text = run_one(cmd, cwd, out_path, args.interval,
                                        label, sim is not None)
        expected, outcome = verdict(passed, text, header.get("witness"),
                                    sim is not None)
        print("%s %s in %ds" % (label, outcome, elapsed), flush=True)
        if expected:
            shutil.rmtree(meta, ignore_errors=True)
            continue
        if not passed:
            print_failure(text, out_path)
        print("  metadir kept: %s" % meta)
        return 1
    print("ALL AS EXPECTED in %ds" % (time.time() - total_started))
    return 0


if __name__ == "__main__":
    sys.exit(main())
