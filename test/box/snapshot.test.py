import os
import yaml
import time
from signal import SIGUSR1

print """#
# A test case for: http://bugs.launchpad.net/bugs/686411
# Check that 'save snapshot' does not overwrite a snapshot
# file that already exists. Verify also that any other
# error that happens when saving snapshot is propagated
# to the caller.
"""
sql("insert into t0 values (1, 'first tuple')")
admin("save snapshot")

# In absence of data modifications, two consecutive
# 'save snapshot' statements will try to write
# into the same file, since file name is based
# on LSN.
#  Don't allow to overwrite snapshots.
admin("save snapshot")
#
# Increment LSN
sql("insert into t0 values (2, 'second tuple')")
#
# Check for other errors, e.g. "Permission denied".
print "# Make 'var' directory read-only."
os.chmod(vardir, 0555)
admin("save snapshot")

# cleanup
os.chmod(vardir, 0755)
sql("delete from t0 where k0 = 1")
sql("delete from t0 where k0 = 2")

print """#
# A test case for http://bugs.launchpad.net/bugs/727174
# "tarantool_box crashes when saving snapshot on SIGUSR1"
#"""

print """
# Increment the lsn number, to make sure there is no such snapshot yet
#"""

sql("insert into t0 values (1, 'Test tuple')")

result = admin("show info", silent=True)
info = yaml.load(result)["info"]

pid = info["pid"]
snapshot = str(info["lsn"]).zfill(20) + ".snap"
snapshot = os.path.join(vardir, snapshot)

iteration = 0

MAX_ITERATIONS = 100

while not os.access(snapshot, os.F_OK) and iteration < MAX_ITERATIONS:
  if iteration % 10 == 0:
    os.kill(pid, SIGUSR1)
  time.sleep(0.1)
  iteration = iteration + 1

if iteration == 0 or iteration >= MAX_ITERATIONS:
  print "Snapshot is missing."
else:
  print "Snapshot exists."

sql("delete from t0 where k0=1")

# vim: syntax=python spell
