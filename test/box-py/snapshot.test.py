from __future__ import print_function

import os
import sys
import yaml
import time
from signal import SIGUSR1

sys.stdout.push_filter(server.vardir, "<dir>")

admin("space = box.schema.space.create('tweedledum')")
admin("index = space:create_index('primary', { type = 'hash' })")

print("""#
# A test case for: http://bugs.launchpad.net/bugs/686411
# Check that 'box.snapshot()' does not overwrite a snapshot
# file that already exists. Verify also that any other
# error that happens when saving snapshot is propagated
# to the caller.
""")

admin("space:insert{1, 'first tuple'}")
admin("box.snapshot()")
#
# Increment LSN
admin("space:insert{2, 'second tuple'}")
#
# Check for other errors, e.g. "Permission denied".
lsn = int(yaml.safe_load(admin("box.info.lsn", silent=True))[0])
snapshot = str(lsn).zfill(20) + ".snap"
snapshot = os.path.join(os.path.join(server.vardir, server.name), snapshot)
# Make snapshot path unwritable
snapshot
os.mkdir(snapshot)
admin("_, e = pcall(box.snapshot)")
admin("e.type")
admin("e.errno")
# Cleanup
os.rmdir(snapshot)

admin("space:delete{1}")
admin("space:delete{2}")

print("""#
# A test case for http://bugs.launchpad.net/bugs/727174
# "tarantool_box crashes when saving snapshot on SIGUSR1"
#""")

print("""
# Increment the lsn number, to make sure there is no such snapshot yet
#""")

admin("space:insert{1, 'Test tuple'}")

pid = int(yaml.safe_load(admin("box.info.pid", silent=True))[0])
lsn = int(yaml.safe_load(admin("box.info.lsn", silent=True))[0])

snapshot = str(lsn).zfill(20) + ".snap"
snapshot = os.path.join(os.path.join(server.vardir, server.name), snapshot)

iteration = 0

MAX_ITERATIONS = 100
while not os.access(snapshot, os.F_OK) and iteration < MAX_ITERATIONS:
  if iteration % 10 == 0:
    os.kill(pid, SIGUSR1)
  time.sleep(0.01)
  iteration = iteration + 1

if iteration == 0 or iteration >= MAX_ITERATIONS:
  print("Snapshot is missing.")
else:
  print("Snapshot exists.")

admin("space:drop()")

sys.stdout.pop_filter()
