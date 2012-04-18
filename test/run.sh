#!/bin/sh

/usr/bin/env python <<__EOB__
import sys
if sys.hexversion < 0x02070000:
    print >>sys.stderr, "Test harness must use python > 2.6"
    exit(1)
else:
    exit(0)
__EOB__

[ $? -eq 0 ] && ./test-run.py $*

