#!/usr/bin/env python3
"""Generate <out dir>/<name>.cfg for every entry of a matrix from template.cfg.

Usage: gen.py <matrix.yaml> <out dir>

The matrices are test.yaml and test_witness.yaml. The output dir holds the
generated configs and the run outputs - runtime files to be deleted when the
matrix is done. Give the two matrices different dirs: they are run against
different modules.

Every config checks TotalInvariant. An entry may add one more check:
`invariant: Name` or `property: Name`, both defined in the module the run
is given (ForwardRaftWitness.tla for the witnesses). An optional
`max_promotes: N` caps the PROMOTEs the cluster writes (MaxPromotes); the
default 999 never binds.

An entry is checked exhaustively unless it has
    simulate:
      traces: N
      depth: D
which makes run.py run TLC's random simulation on it instead: N behaviors
of up to D steps (D is optional, TLC's default is 100). `traces` is
required - it is what bounds the run. The generated config carries run.py's
options for it in a header comment, written only when there are any:
    \\* run: {"simulate": {"traces": N, "depth": D}}
run.py reads the JSON after `run:`, so one run goes through a mixed matrix.

Node ids are n1..nN: the data nodes first, the voters after them. The limbo
owner at start is n1. Transactions are single letters a, b, c, ...
"""
import json
import os
import string
import sys

import yaml


def tla_set(items):
    return "{" + ", ".join(items) + "}"


def checks(cfg):
    """The INVARIANTS and PROPERTIES sections of the config."""
    lines = ["INVARIANTS", "    TotalInvariant"]
    if "invariant" in cfg:
        lines.append("    " + cfg["invariant"])
    if "property" in cfg:
        lines += ["", "PROPERTIES", "    " + cfg["property"]]
    return "\n".join(lines)


def run_line(cfg):
    """The `\\* run: <json>` header line with run.py's options for this
    config, empty when there are none."""
    opts = {}
    if "simulate" in cfg:
        sim = cfg["simulate"]
        if not isinstance(sim, dict) or "traces" not in sim or \
                not set(sim) <= {"traces", "depth"} or \
                not all(isinstance(v, int) for v in sim.values()):
            sys.exit("%s: simulate takes traces (required) and depth, integers"
                     % cfg["name"])
        opts["simulate"] = sim
    if not opts:
        return ""
    return "\\* run: %s\n" % json.dumps(opts)


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip())
        return 2
    here = os.path.dirname(os.path.abspath(__file__))
    matrix_path = argv[1]
    matrix_name = os.path.basename(matrix_path)
    out_dir = argv[2]
    with open(matrix_path) as f:
        matrix = yaml.safe_load(f)
    with open(os.path.join(here, "template.cfg")) as f:
        template = f.read()
    os.makedirs(out_dir, exist_ok=True)
    for cfg in matrix["configs"]:
        total = cfg["data_nodes"] + cfg["voters"]
        nodes = ["n%d" % i for i in range(1, total + 1)]
        voters = nodes[cfg["data_nodes"]:]
        txns = list(string.ascii_lowercase[:cfg["transactions"]])
        text = template.format(
            matrix=matrix_name,
            note=cfg.get("note", ""),
            run_line=run_line(cfg),
            node_ids=tla_set(nodes),
            voter_ids=tla_set(voters),
            transactions=tla_set(txns),
            max_term=cfg["max_term"],
            max_promotes=cfg.get("max_promotes", 999),
            quorum=cfg["quorum"],
            checks=checks(cfg),
        )
        path = os.path.join(out_dir, cfg["name"] + ".cfg")
        with open(path, "w") as f:
            f.write(text)
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
