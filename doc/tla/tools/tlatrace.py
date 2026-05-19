#!/usr/bin/env python3
"""Read a TLC error trace and show what each step changed.

Usage:
    tlatrace.py OUT [diff]              per-step diffs (the default)
    tlatrace.py OUT actions             the action sequence only
    tlatrace.py OUT show N [PATH]       state N (1-based), or its sub-value
                                        at PATH like Nodes.n1.limbo_promotions
    tlatrace.py OUT json                the whole trace as JSON

OUT is the TLC output containing "State N: <Action ...>" blocks. Records and
functions become dicts, sequences become lists, sets become sorted lists
under a {"$set": [...]} wrapper, model values become strings.
"""
import json
import re
import sys


# ----------------------------------------------------------------------------
# TLA+ value parser
# ----------------------------------------------------------------------------

TOKEN_RE = re.compile(r"""
    \s*(?:
        (?P<str>"(?:[^"\\]|\\.)*")
      | (?P<num>-?\d+)
      | (?P<op><<|>>|\|->|:>|@@|[()\[\]{},])
      | (?P<id>[A-Za-z_][A-Za-z0-9_]*)
    )""", re.VERBOSE)


def tokenize(text):
    pos = 0
    tokens = []
    while pos < len(text):
        m = TOKEN_RE.match(text, pos)
        if m is None or m.end() == pos:
            if text[pos:].strip() == "":
                break
            raise ValueError("bad token at: %r" % text[pos:pos + 40])
        pos = m.end()
        kind = m.lastgroup
        tokens.append((kind, m.group(kind)))
    return tokens


class Parser:
    def __init__(self, text):
        self.tokens = tokenize(text)
        self.i = 0

    def peek(self):
        return self.tokens[self.i] if self.i < len(self.tokens) else (None, None)

    def take(self, expect=None):
        kind, val = self.peek()
        if expect is not None and val != expect:
            raise ValueError("expected %r, got %r" % (expect, val))
        self.i += 1
        return kind, val

    def value(self):
        kind, val = self.peek()
        if kind == "str":
            self.take()
            return val[1:-1]
        if kind == "num":
            self.take()
            return int(val)
        if kind == "id":
            self.take()
            if val == "TRUE":
                return True
            if val == "FALSE":
                return False
            return val
        if val == "[":
            return self.record()
        if val == "(":
            return self.function()
        if val == "{":
            return self.set()
        if val == "<<":
            return self.sequence()
        raise ValueError("unexpected token %r" % (val,))

    def record(self):
        self.take("[")
        out = {}
        while self.peek()[1] != "]":
            _, name = self.take()
            self.take("|->")
            out[name] = self.value()
            if self.peek()[1] == ",":
                self.take()
        self.take("]")
        return out

    def function(self):
        self.take("(")
        out = {}
        while self.peek()[1] != ")":
            key = self.value()
            self.take(":>")
            out[str(key)] = self.value()
            if self.peek()[1] == "@@":
                self.take()
        self.take(")")
        return out

    def set(self):
        self.take("{")
        items = []
        while self.peek()[1] != "}":
            items.append(self.value())
            if self.peek()[1] == ",":
                self.take()
        self.take("}")
        return {"$set": sorted(items, key=json.dumps)}

    def sequence(self):
        self.take("<<")
        items = []
        while self.peek()[1] != ">>":
            items.append(self.value())
            if self.peek()[1] == ",":
                self.take()
        self.take(">>")
        return items


def parse_value(text):
    p = Parser(text)
    v = p.value()
    if p.i != len(p.tokens):
        raise ValueError("trailing tokens: %r" % (p.tokens[p.i:p.i + 5],))
    return v


# ----------------------------------------------------------------------------
# Trace reader
# ----------------------------------------------------------------------------

STATE_RE = re.compile(r"^State (\d+): <(.*?)(?: line \d+, col .*)?>\s*$")


def read_trace(path):
    states = []
    cur = None
    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            m = STATE_RE.match(line)
            if m:
                cur = {"n": int(m.group(1)), "action": m.group(2), "vars": {}}
                states.append(cur)
                continue
            if cur is not None and line.startswith("/\\ "):
                name, _, rest = line[3:].partition(" = ")
                cur["vars"][name] = parse_value(rest)
                continue
            if cur is not None and line.strip() == "":
                cur = None
    return states


# ----------------------------------------------------------------------------
# Diff
# ----------------------------------------------------------------------------

def is_set(v):
    return isinstance(v, dict) and set(v.keys()) == {"$set"}


def fmt(v):
    return json.dumps(v, separators=(",", ":"))


def diff(old, new, path, out):
    if is_set(old) and is_set(new):
        o = {fmt(x): x for x in old["$set"]}
        n = {fmt(x): x for x in new["$set"]}
        for k in sorted(set(n) - set(o)):
            out.append("%s: +%s" % (path, k))
        for k in sorted(set(o) - set(n)):
            out.append("%s: -%s" % (path, k))
        return
    if isinstance(old, dict) and isinstance(new, dict) and \
       not is_set(old) and not is_set(new):
        # Different key sets mean a replaced object, like an empty
        # promotion slot getting filled. Show it whole.
        if set(old) != set(new):
            out.append("%s: %s -> %s" % (path, fmt(old), fmt(new)))
            return
        for k in sorted(old):
            sub = "%s.%s" % (path, k) if path else k
            diff(old[k], new[k], sub, out)
        return
    if isinstance(old, list) and isinstance(new, list):
        common = min(len(old), len(new))
        for i in range(common):
            if old[i] != new[i]:
                diff(old[i], new[i], "%s[%d]" % (path, i + 1), out)
        for i in range(common, len(new)):
            out.append("%s: +%s" % (path, fmt(new[i])))
        for i in range(common, len(old)):
            out.append("%s: -%s" % (path, fmt(old[i])))
        return
    if old != new:
        out.append("%s: %s -> %s" % (path, fmt(old), fmt(new)))


def cmd_diff(states):
    for prev, cur in zip([None] + states, states):
        print("== State %d: %s" % (cur["n"], cur["action"]))
        if prev is None:
            continue
        changes = []
        diff(prev["vars"], cur["vars"], "", changes)
        for c in changes:
            print("   " + c)


def cmd_actions(states):
    for s in states:
        print("%3d  %s" % (s["n"], s["action"]))


def cmd_show(states, n, path):
    v = states[n - 1]["vars"]
    for part in [p for p in path.split(".") if p]:
        if isinstance(v, list):
            v = v[int(part) - 1]
        else:
            v = v[part]
    print(json.dumps(v, indent=2))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    states = read_trace(argv[1])
    cmd = argv[2] if len(argv) > 2 else "diff"
    if cmd == "diff":
        cmd_diff(states)
    elif cmd == "actions":
        cmd_actions(states)
    elif cmd == "show":
        cmd_show(states, int(argv[3]), argv[4] if len(argv) > 4 else "")
    elif cmd == "json":
        print(json.dumps(states, indent=2))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
