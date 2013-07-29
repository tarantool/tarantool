# encoding: utf-8
#
import subprocess
import sys
import os

p = subprocess.Popen([ os.path.join(builddir, "test/box/protocol") ],
                     stdout=subprocess.PIPE)
p.wait()
for line in p.stdout.readlines():
      sys.stdout.write(line)

sql("delete from t0 where k0 = 1")
# vim: syntax=python
