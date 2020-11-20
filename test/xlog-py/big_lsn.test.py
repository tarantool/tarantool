import os

#
# Check that Tarantool handles huge LSNs well (gh-4033).
#

# Fill an empty directory.
server.stop()
server.deploy()
server.admin("box.info.lsn")
server.admin("box.space._schema:delete('dummy')")
server.stop()

# Bump the instance vclock by tweaking the last xlog.
old_lsn = 1
new_lsn = 123456789123
wal_dir = os.path.join(server.vardir, server.name)
old_wal = os.path.join(wal_dir, "%020d.xlog" % old_lsn)
new_wal = os.path.join(wal_dir, "%020d.xlog" % new_lsn)
with open(old_wal, "r+") as f:
    s = f.read()
    s = s.replace("VClock: {{1: {}}}".format(old_lsn),
                  "VClock: {{1: {}}}".format(new_lsn))
    f.seek(0)
    f.write(s)
os.rename(old_wal, new_wal)

# Recover and make a snapshot.
server.start()
server.admin("box.info.lsn")
server.admin("box.space._schema:delete('dummy')")
server.admin("box.snapshot()")
server.stop()

# Try one more time.
server.start()
server.admin("box.info.lsn")
server.admin("box.space._schema:delete('dummy')")
server.admin("box.snapshot()")
