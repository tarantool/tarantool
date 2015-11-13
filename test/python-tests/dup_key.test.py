import os
import yaml

#print """
#A test case for https://bugs.launchpad.net/tarantool/+bug/1052018
#panic_on_wal_error doesn't work for duplicate key errors
#"""

server.stop()
server.deploy()
lsn = int(yaml.load(server.admin("box.info.server.lsn", silent=True))[0])
filename = str(lsn).zfill(20) + ".xlog"
vardir = os.path.join(server.vardir, server.name)
wal_old = os.path.join(vardir, "old_" + filename)
wal = os.path.join(vardir, filename)

# Create wal#1
server.admin("space = box.schema.space.create('test')")
server.admin("index = box.space.test:create_index('primary')")
server.admin("box.space.test:insert{1, 'first tuple'}")
server.admin("box.space.test:insert{2, 'second tuple'}")
server.stop()

# Save wal #1
if os.access(wal, os.F_OK):
    print ".xlog exists"
    os.rename(wal, wal_old)

lsn += 4

# Create another wal#1
server.start()
server.admin("space = box.schema.space.create('test')")
server.admin("index = box.space.test:create_index('primary')")
server.admin("box.space.test:insert{1, 'first tuple'}")
server.admin("box.space.test:delete{1}")
server.stop()

# Create wal#2
server.start()
server.admin("box.space.test:insert{1, 'third tuple'}")
server.admin("box.space.test:insert{2, 'fourth tuple'}")
server.stop()

if os.access(wal, os.F_OK):
    print ".xlog exists"
    # Replace wal#1 with saved copy
    os.unlink(wal)
    os.rename(wal_old, wal)


server.start()
line = 'Duplicate key'
print "check log line for '%s'" % line
print
if server.logfile_pos.seek_once(line) >= 0:
    print "'%s' exists in server log" % line
print

server.admin("box.space.test:get{1}")
server.admin("box.space.test:get{2}")
server.admin("box.space.test:len()")

