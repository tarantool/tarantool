import os
import sys
import re
import yaml
from lib.tarantool_server import TarantoolServer

print '-------------------------------------------------------------'
print 'gh-434: Assertion if replace _cluster tuple'
print '-------------------------------------------------------------'

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

# Invalid UUID
server.admin("box.space._cluster:replace{1, require('uuid').NULL:str()}")

# Cleanup
server.stop()
server.deploy()

print '-------------------------------------------------------------'
print 'gh-527: update vclock on delete from box.space._cluster'
print '-------------------------------------------------------------'

# master server
master = server
master_id = master.get_param('server')['id']

master.admin("box.schema.user.grant('guest', 'read,write,execute', 'universe')")

replica = TarantoolServer(server.ini)
replica.script = 'replication/replica.lua'
replica.vardir = os.path.join(server.vardir, 'replica')
replica.rpl_master = master
replica.deploy()
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica_id = replica.get_param('server')['id']
replica_uuid = replica.get_param('server')['uuid']
sys.stdout.push_filter(replica_uuid, '<replica uuid>')
replica.admin('box.space._schema:insert{"test", 48}')

replica.admin('box.info.server.id')
replica.admin('box.info.server.ro')
replica.admin('box.info.server.lsn') # 1
replica.admin('box.info.vclock[%d]' % replica_id)

master.admin('box.space._cluster:delete{%d}' % replica_id)
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica.admin('box.info.server.id')
replica.admin('box.info.server.ro')
replica.admin('box.info.server.lsn') # -1
replica.admin('box.info.vclock[%d]' % replica_id)
# replica is read-only
replica.admin('box.space._schema:replace{"test", 48}')

replica_id2 = 10
master.admin('box.space._cluster:insert{%d, "%s"}' %
    (replica_id2, replica_uuid))
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica.admin('box.info.server.id')
replica.admin('box.info.server.ro')
replica.admin('box.info.server.lsn') # 0
replica.admin('box.info.vclock[%d]' % replica_id)
replica.admin('box.info.vclock[%d]' % replica_id2)

replica_id3 = 11
server.admin("box.space._cluster:update(%d, {{'=', 1, %d}})" %
    (replica_id2, replica_id3))
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica.admin('box.info.server.id')
replica.admin('box.info.server.ro')
replica.admin('box.info.server.lsn') # 0
replica.admin('box.info.vclock[%d]' % replica_id)
replica.admin('box.info.vclock[%d]' % replica_id2)
replica.admin('box.info.vclock[%d]' % replica_id3)

sys.stdout.pop_filter()

# Cleanup
server.stop()
server.deploy()
