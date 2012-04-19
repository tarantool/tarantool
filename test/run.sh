#!/bin/sh

/usr/bin/env python <<__EOB__
import sys
if sys.hexversion < 0x02070000 or sys.hexversion >= 0x03000000:
    sys.stderr.write("ERROR: test harness must use python >= 2.6 but not 3.x\n")
    sys.exit(1)
else:
    sys.exit(0)
__EOB__

[ $? -eq 0 ] && ./test-run.py $*

