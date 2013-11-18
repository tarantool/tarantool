# encoding: utf-8
#
import os

print """
# Verify that the server starts from a pre-recorded snapshot.
# This way we check that the server can read old snapshots (v11)
# going forward.
"""
server.stop()
snapshot = os.path.join(vardir, "00000000000000000500.snap")
os.symlink(os.path.abspath("box/00000000000000000500.snap"), snapshot)
server.start()
for i in range(0, 501):
  sql("select * from t0 where k0={0}".format(i))
print "# Restore the default server..."
server.stop()
os.unlink(snapshot)
server.start()

# vim: syntax=python spell
