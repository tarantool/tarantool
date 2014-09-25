import re
import yaml

#
# gh-434: Assertion if replace _cluster tuple
#

new_uuid = '8c7ff474-65f9-4abe-81a4-a3e1019bb1ae'

# Requires panic_on_wal_error = false
server.admin("box.space._cluster:replace{{1, '{}'}}".format(new_uuid))
server.admin("box.info.server.uuid")

# Check log message
server.stop()
f = open(server.logfile, "r")
f.seek(0, 2)
server.start()

check="server uuid changed to " + new_uuid
print "check log line for '%s'" % check
print
line = f.readline()
while line:
    if re.search(r'(%s)' % check, line):
        print "'%s' exists in server log" % check
        break
    line = f.readline()
print
f.close()
server.admin("box.info.server.uuid")

# Check that new UUID has been saved in snapshot
server.admin("box.snapshot()")
server.restart()

server.admin("box.info.server.uuid")

# Can't reset server id
server.admin("box.space._cluster:delete(1)")
server.admin("box.space._cluster:update(1, {{'=', 1, 10}})")

# Invalid UUID
server.admin("box.space._cluster:replace{1, require('uuid').NULL:str()}")

# Cleanup
server.stop()
server.deploy()
