#!/usr/bin/env python3
"""Generate the TLC runs of a matrix, or of a scripts dir, into <out dir>.

Usage: gen.py <matrix.yaml> <out dir>
       gen.py <scripts dir> <out dir>

A run is a pair <out dir>/<name>.tla and <out dir>/<name>.cfg: the module
TLC checks and its config, from the templates in test/. The module extends
the one the source names and carries, above its module line, a YAML header
with what tools/run.py needs to run it:
    library: the dirs of the extended modules, relative to the file
    witness: NotReached, the check a witness run must end violating
    simulate: {traces: N, depth: D}, when the run is a simulation
run.py takes the .tla files, finds the .cfg beside each and passes the
library dirs to TLC. The output dir holds the generated files and the run
outputs - runtime files to be deleted when the matrix is done. Give every
source its own dir.

A matrix (test/test.yaml, test/test_witness.yaml) names the module its runs
extend, `spec`, relative to this dir, and lists the runs. Every run checks
TotalInvariant. An entry may add one more check, `invariant: Name` or
`property: Name`, defined in that module. A witness entry instead names the
scenario the model must reach: `witness: Name`, a state predicate, or
`step_witness: Name`, an action over Nodes and Nodes'. The generated module
negates it as NotReached, an invariant or an action property, so TLC stops
with the trace reaching the scenario, and run.py expects exactly that. An
optional `max_promotes: N` caps the PROMOTEs the cluster writes
(MaxPromotes); the default 999 never binds.

An entry is checked exhaustively unless it has
    simulate:
      traces: N
      depth: D
which makes run.py run TLC's random simulation on it instead: N behaviors
of up to D steps (D is optional, TLC's default is 100). `traces` is
required - it is what bounds the run.

Node ids are n1..nN: the data nodes first, the voters after them. The limbo
owner at start is n1. Transactions are single letters a, b, c, ...

A script (test/scripted) is a .tla file with the config above its module
line and the steps below it:
    data_nodes: 3
    ...
    ---- MODULE <name> ----
    NodeBumpTerm(n1)
    ...
    ====
The text above the module line is YAML, the keys of a matrix entry; TLA+
tools ignore everything outside the module, and the editors highlight the
inside, so the file reads as TLA+ although it isn't a module TLC could run.
The module's name is the file's. The steps are the spec's actions, or the
pinning helpers of VRaftScript.tla, one call per line in the order they
happen - the i-th such line is step i. A line `Expect(predicate)` is a
checkpoint, not a step: the predicate must hold in the state after the step
above it; together they are the Checkpoints invariant. Comments and blank
lines are free, the end line `====` is optional. The generated module
declares the node and transaction names as constants, so the steps can name
them, and the config assigns them, which is why a script runs with no
symmetry.

After its last step a script stops and the run ends by the deadlock, unless
the header says to go on with the spec's own Next:
    explore:
      depth: D
the exhaustive search D steps deep from the script's end, bounded by a
constraint on the step; or `simulate` as above, random walks of D steps
from the script's end. Both together bound the walks by the constraint too.
"""
import os
import string
import sys

import yaml

COMMENT = "\\*"
REQUIRED = ("data_nodes", "voters", "quorum", "max_term", "transactions")


def is_call(text):
    """Whether the line is one call: a name and its arguments in parens -
    an action, a helper, or Expect."""
    name, paren, _ = text.partition("(")
    return paren != "" and name.isidentifier() and text.endswith(")")


def tla_set(items):
    return "{" + ", ".join(items) + "}"


def names(cfg):
    """The node ids and the transaction names of the config."""
    total = cfg["data_nodes"] + cfg["voters"]
    nodes = ["n%d" % i for i in range(1, total + 1)]
    txns = list(string.ascii_lowercase[:cfg["transactions"]])
    return nodes, txns


def checks(cfg, invariants=(), properties=()):
    """The INVARIANTS and PROPERTIES sections of the config: TotalInvariant,
    the given names, and the entry's own `invariant` and `property`."""
    invariants = ["TotalInvariant"] + list(invariants)
    if "invariant" in cfg:
        invariants.append(cfg["invariant"])
    properties = list(properties)
    if "property" in cfg:
        properties.append(cfg["property"])
    lines = ["INVARIANTS"] + ["    " + name for name in invariants]
    if properties:
        lines += ["", "PROPERTIES"] + ["    " + name for name in properties]
    return "\n".join(lines)


def witness_of(cfg):
    """The scenario of a witness entry as (name, is_step), None otherwise."""
    if "witness" in cfg and "step_witness" in cfg:
        sys.exit("%s: witness or step_witness, not both" % cfg["name"])
    if "witness" in cfg:
        return cfg["witness"], False
    if "step_witness" in cfg:
        return cfg["step_witness"], True
    return None


def simulate_options(cfg, script_len=0):
    """The validated `simulate` options of the entry, None without them. For
    a script the depth counts from the script's end."""
    sim = cfg.get("simulate")
    if sim is None:
        return None
    if not isinstance(sim, dict) or "traces" not in sim or \
            not set(sim) <= {"traces", "depth"} or \
            not all(isinstance(v, int) for v in sim.values()):
        sys.exit("%s: simulate takes traces (required) and depth, integers"
                 % cfg["name"])
    sim = dict(sim)
    if script_len:
        sim["depth"] = sim.get("depth", 100) + script_len
    return sim


def explore_depth(cfg):
    """The `explore` depth of a script, None without it."""
    explore = cfg.get("explore")
    if explore is None:
        return None
    if not isinstance(explore, dict) or set(explore) != {"depth"} or \
            not isinstance(explore["depth"], int):
        sys.exit("%s: explore takes depth, an integer" % cfg["name"])
    return explore["depth"]


def header_text(source, note, library, witness=None, sim=None):
    """The YAML above the module line of a generated module - what run.py
    reads: the library dirs, the check a witness run must end violating,
    the simulation."""
    opts = {"library": library}
    if witness is not None:
        opts["witness"] = witness
    if sim is not None:
        opts["simulate"] = sim
    return ("# Generated from %s by gen.py - do not edit, edit the source.\n"
            "# %s\n%s" % (source, note, yaml.safe_dump(opts, sort_keys=False)))


def library_dirs(here, out_dir):
    """The dirs TLC finds the extended modules in, relative to the output
    dir: this one, with the spec, and test/, with the test modules."""
    return [os.path.relpath(d, out_dir) for d in (here, os.path.join(here, "test"))]


def write(path, text):
    with open(path, "w") as f:
        f.write(text)
    print(path)


def render_cfg(template, source, cfg, spec, model_values, checks_text,
               reductions):
    nodes, txns = names(cfg)
    return template.format(
        source=source,
        note=cfg.get("note", ""),
        spec=spec,
        node_ids=tla_set(nodes),
        voter_ids=tla_set(nodes[cfg["data_nodes"]:]),
        transactions=tla_set(txns),
        max_term=cfg["max_term"],
        max_promotes=cfg.get("max_promotes", 999),
        quorum=cfg["quorum"],
        model_values=model_values,
        checks=checks_text,
        reductions=reductions,
    )


def module_name(line):
    """The name of a `---- MODULE name ----` line, None for any other."""
    if not line.startswith("----"):
        return None
    words = line.strip("- ").split()
    if len(words) != 2 or words[0] != "MODULE":
        return None
    return words[1]


def parse_script(path):
    """The config of the script, its steps as (line, text) and its
    checkpoints as (the step it follows, text)."""
    name = os.path.basename(path)[:-len(".tla")]
    with open(path) as f:
        lines = f.read().split("\n")
    i = 0
    while i < len(lines) and module_name(lines[i]) is None:
        i += 1
    if i == len(lines):
        sys.exit("%s: no module line: the config above it, the steps below"
                 % path)
    if module_name(lines[i]) != name:
        sys.exit("%s:%d: the module is named after the file, %s"
                 % (path, i + 1, name))
    try:
        cfg = yaml.safe_load("\n".join(lines[:i]))
    except yaml.YAMLError as e:
        sys.exit("%s: the config above the module line is not valid YAML: %s"
                 % (path, e))
    if not isinstance(cfg, dict):
        sys.exit("%s: the config above the module line must be a YAML mapping"
                 % path)
    missing = [k for k in REQUIRED if k not in cfg]
    if missing:
        sys.exit("%s: the config lacks %s" % (path, ", ".join(missing)))
    steps, expects = [], []
    for n, line in enumerate(lines[i + 1:], i + 2):
        if line.startswith("===="):
            break
        text = line.split(COMMENT)[0].strip()
        if not text:
            continue
        if not is_call(text):
            sys.exit("%s:%d: a step is one call of an action or a helper: %r"
                     % (path, n, text))
        if text.startswith("Expect("):
            expects.append((len(steps), text))
        else:
            steps.append((n, text))
    if not steps:
        sys.exit("%s: no steps" % path)
    return cfg, steps, expects


def render_module(template, source, name, cfg, steps, expects, goes_on, depth):
    nodes, txns = names(cfg)
    cases = []
    for i, (line, text) in enumerate(steps, 1):
        prefix = "" if i == 1 else "      [] "
        cases.append("%si = %d -> %s  %s line %d" % (prefix, i, text, COMMENT, line))
    checkpoints = ["    /\\ step = %d => %s" % (after + 1, text)
                   for after, text in expects] or ["    TRUE"]
    run = "ScriptRun ==\n    \\/ ScriptNext(ScriptLength, Step)\n"
    if goes_on:
        run += "    \\/ ExploreNext(ScriptLength)\n"
    bound = ""
    if depth is not None:
        bound = ("\n%s The explore depth of the header: states past the "
                 "script's end.\nStepBound == step <= ScriptLength + 1 + %d\n"
                 % (COMMENT, depth))
    return template.format(
        name=name,
        source=source,
        constants=", ".join(nodes + txns),
        length=len(steps),
        cases="\n".join(cases),
        checkpoints="\n".join(checkpoints),
        run=run,
        bound=bound,
    )


def gen_matrix(matrix_path, out_dir, templates, here):
    with open(matrix_path) as f:
        matrix = yaml.safe_load(f)
    source = os.path.basename(matrix_path)
    if "spec" not in matrix:
        sys.exit("%s: the matrix must name the module its runs extend in "
                 "`spec`, relative to %s" % (matrix_path, here))
    extends = os.path.basename(matrix["spec"])[:-len(".tla")]
    library = library_dirs(here, out_dir)
    for cfg in matrix["configs"]:
        name = cfg["name"]
        witness = witness_of(cfg)
        block, invariants, properties = "", [], []
        if witness is not None:
            scenario, is_step = witness
            negated = "[][~%s]_vars" if is_step else "~%s"
            block = ("\n%s The scenario, negated: TLC stops with the trace "
                     "reaching it.\nNotReached == %s\n\n"
                     % (COMMENT, negated % scenario))
            (properties if is_step else invariants).append("NotReached")
        module = templates["run"].format(name=name, extends=extends,
                                         witness=block)
        header = header_text(source, cfg.get("note", ""), library,
                             witness and "NotReached", simulate_options(cfg))
        write(os.path.join(out_dir, name + ".tla"), header + module)
        text = render_cfg(templates["cfg"], source, cfg, "Spec", "",
                          checks(cfg, invariants, properties),
                          "SYMMETRY Perms\n\n")
        write(os.path.join(out_dir, name + ".cfg"), text)


def gen_scripts(scripts_dir, out_dir, templates, here):
    library = library_dirs(here, out_dir)
    for fname in sorted(os.listdir(scripts_dir)):
        if not fname.endswith(".tla"):
            continue
        name = fname[:-len(".tla")]
        source = os.path.join(os.path.basename(scripts_dir), fname)
        cfg, steps, expects = parse_script(os.path.join(scripts_dir, fname))
        cfg["name"] = name
        if "spec" in cfg:
            sys.exit("%s: a script's module is generated, it names none" % fname)
        if witness_of(cfg) is not None:
            sys.exit("%s: a script dictates its scenario, it witnesses none"
                     % fname)
        sim = simulate_options(cfg, len(steps))
        depth = explore_depth(cfg)
        goes_on = sim is not None or depth is not None
        nodes, txns = names(cfg)
        module = render_module(templates["script"], source, name, cfg, steps,
                               expects, goes_on, depth)
        header = header_text(source, cfg.get("note", ""), library, None, sim)
        write(os.path.join(out_dir, fname), header + module)
        model_values = "".join("    %s = %s\n" % (v, v) for v in nodes + txns)
        reductions = "CONSTRAINT StepBound\n\n" if depth is not None else ""
        text = render_cfg(templates["cfg"], source, cfg, "ScriptSpec",
                          model_values, checks(cfg, ["Checkpoints"]),
                          reductions)
        write(os.path.join(out_dir, name + ".cfg"), text)


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip())
        return 2
    here = os.path.dirname(os.path.abspath(__file__))
    source, out_dir = argv[1], argv[2]
    templates = {}
    for kind, fname in (("cfg", "template.cfg"), ("run", "template_run.tla"),
                        ("script", "template_script.tla")):
        with open(os.path.join(here, "test", fname)) as f:
            templates[kind] = f.read()
    os.makedirs(out_dir, exist_ok=True)
    if os.path.isdir(source):
        gen_scripts(source, out_dir, templates, here)
    else:
        gen_matrix(source, out_dir, templates, here)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
