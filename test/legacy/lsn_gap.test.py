import os
import yaml
#
# gh-167: Replica can't find next xlog file if there is a gap in LSN
#

server.stop()
server.deploy()

# Create wal#1
server.admin("space = box.schema.space.create('test')")
server.admin("index = box.space.test:create_index('primary')")
server.admin("box.space.test:insert{1, 'first tuple'}")
server.admin("box.space.test:insert{2, 'second tuple'}")
lsn = int(yaml.load(server.admin("box.info.server.lsn", silent=True))[0])
path = os.path.join(server.vardir, server.name)
wal = os.path.join(path, str(lsn).zfill(20) + ".xlog")
server.stop()
server.start()
server.admin("box.space.test:insert{3, 'third tuple'}")
server.stop()
server.start()
server.admin("box.space.test:insert{4, 'fourth tuple'}")
server.stop()

# Remove xlog with {3, 'third tuple'}
os.unlink(wal)

server.start()
line="ignoring a gap in LSN"
print "check log line for '%s'" % line
print
if server.logfile_pos.seek_once(line) >= 0:
    print "'%s' exists in server log" % line
print

# missing tuple from removed xlog
server.admin("box.space.test:select{}")

