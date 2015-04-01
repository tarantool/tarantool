#!/usr/bin/env python
import os
import glob
import json

a = glob.glob("*.json")

for f in a:
    open(f + ".out", 'w').write(json.dumps(json.loads(open(f, 'r').read()), sort_keys=True, indent=4))
