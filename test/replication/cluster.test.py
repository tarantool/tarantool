import os
import sys
import re
import yaml
import uuid
import glob
from lib.tarantool_server import TarantoolServer

## Get cluster uuid
cluster_uuid = ''
try:
    cluster_uuid = yaml.load(server.admin("box.space._schema:get('cluster')",
        silent = True))[0][1]
    uuid.UUID('{' + cluster_uuid + '}')
    print 'ok - cluster uuid'
except Exception as e:
    print 'not ok - invalid cluster uuid', e

print '-------------------------------------------------------------'
print ' gh-696: Check global READ permissions for replication'
print '-------------------------------------------------------------'

# Generate replica cluster UUID
replica_uuid = str(uuid.uuid4())

## Universal read permission is required to perform JOIN/SUBSCRIBE
rows = list(server.iproto.py_con.join(replica_uuid))
print len(rows) == 1 and rows[0].return_message.find('Read access') >= 0 and \
    'ok' or 'not ok', '-', 'join without read permissions to universe'
rows = list(server.iproto.py_con.subscribe(cluster_uuid, replica_uuid))
print len(rows) == 1 and rows[0].return_message.find('Read access') >= 0 and \
    'ok' or 'not ok', '-', 'subscribe without read permissions to universe'

## Write permission to space `_cluster` is required to perform JOIN
server.admin("box.schema.user.grant('guest', 'read', 'universe')")
server.iproto.py_con.close() # re-connect with new permissions
rows = list(server.iproto.py_con.join(replica_uuid))
print len(rows) == 1 and rows[0].return_message.find('Write access') >= 0 and \
    'ok' or 'not ok', '-', 'join without write permissions to _cluster'

def check_join(msg):
    ok = True
    for resp in server.iproto.py_con.join(replica_uuid):
        if resp.completion_status != 0:
            print 'not ok', '-', msg, resp.return_message
            ok = False

    server.iproto.py_con.close() # JOIN brokes protocol
    if not ok:
        return
    tuples = server.iproto.py_con.space('_cluster').select(replica_uuid, index = 1)
    if len(tuples) == 0:
        print 'not ok', '-', msg, 'missing entry in _cluster'
        return
    server_id = tuples[0][0]
    print 'ok', '-', msg
    return server_id

## JOIN with permissions
server.admin("box.schema.user.grant('guest', 'write', 'space', '_cluster')")
server.iproto.py_con.close() # re-connect with new permissions
server_id = check_join('join with granted permissions')
server.iproto.py_con.space('_cluster').delete(server_id)

# JOIN with granted role
server.admin("box.schema.user.revoke('guest', 'read', 'universe')")
server.admin("box.schema.user.revoke('guest', 'write', 'space', '_cluster')")
server.admin("box.schema.user.grant('guest', 'replication')")
server.iproto.py_con.close() # re-connect with new permissions
server_id = check_join('join with granted role')
server.iproto.py_con.space('_cluster').delete(server_id)

print '-------------------------------------------------------------'
print 'gh-707: Master crashes on JOIN if it does not have snapshot files'
print 'gh-480: If socket is closed while JOIN, replica wont reconnect'
print '-------------------------------------------------------------'

data_dir = os.path.join(server.vardir, server.name)
for k in glob.glob(os.path.join(data_dir, '*.snap')):
    os.unlink(k)

# remember the number of servers in _cluster table
server_count = len(server.iproto.py_con.space('_cluster').select(()))

rows = list(server.iproto.py_con.join(replica_uuid))
print len(rows) == 1 and rows[0].return_message.find('snapshot') >= 0 and \
    'ok' or 'not ok', '-', 'join without snapshots'

print server_count == len(server.iproto.py_con.space('_cluster').select(())) and\
    'ok' or 'not ok', '-', '_cluster does not changed after unsuccessful JOIN'

server.admin("box.schema.user.revoke('guest', 'replication')")
server.admin('box.snapshot()')

print '-------------------------------------------------------------'
print 'gh-434: Assertion if replace _cluster tuple'
print '-------------------------------------------------------------'
server.stop()
script = server.script
server.script = "replication/panic.lua"
server.deploy()

new_uuid = '8c7ff474-65f9-4abe-81a4-a3e1019bb1ae'

# Check log message
# Requires panic_on_wal_error = false
server.admin("box.space._cluster:replace{{1, '{0}'}}".format(new_uuid))
server.admin("box.info.server.uuid")

line = "server UUID changed to " + new_uuid
print "check log line for '%s'" % line
print
if server.logfile_pos.seek_once(line) >= 0:
    print "'%s' exists in server log" % line
print
server.admin("box.info.server.uuid")

# Check that new UUID has been saved in snapshot
server.admin("box.snapshot()")
server.restart()

server.admin("box.info.server.uuid")

# Invalid UUID
server.admin("box.space._cluster:replace{1, require('uuid').NULL:str()}")

# Cleanup
server.stop()
server.script = script
server.deploy()

print '-------------------------------------------------------------'
print 'gh-527: update vclock on delete from box.space._cluster'
print '-------------------------------------------------------------'

# master server
master = server
master_id = master.get_param('server')['id']

master.admin("box.schema.user.grant('guest', 'replication')")

replica = TarantoolServer(server.ini)
replica.script = 'replication/replica.lua'
replica.vardir = server.vardir
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
# Tuple is read-only
server.admin("box.space._cluster:update(%d, {{'=', 1, %d}})" %
    (replica_id2, replica_id3))
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica.admin('box.info.server.id')
replica.admin('box.info.server.ro')
replica.admin('box.info.server.lsn') # 0
replica.admin('box.info.vclock[%d]' % replica_id)
replica.admin('box.info.vclock[%d]' % replica_id2)
replica.admin('box.info.vclock[%d]' % replica_id3)
replica.stop()
replica.cleanup(True)

print '-------------------------------------------------------------'
print 'gh-806: cant prune old replicas by deleting their server ids'
print '-------------------------------------------------------------'

# Rotate xlog
master.restart()
master.admin("box.space._schema:insert{'test', 1}")

# Prune old replicas
master.admin("cluster_len = box.space._cluster:len()")
# Delete from _cluster for replicas with lsn=0 is safe
master.admin('for id, lsn in pairs(box.info.vclock) do'
             ' if id ~= box.info.server.id then box.space._cluster:delete{id} end '
             'end');
master.admin("box.space._cluster:len() < cluster_len")

# Save a snapshot without removed replicas in vclock
master.admin("box.snapshot()")

# Master is not crashed then recovering xlog with {replica_id: 0} in header
master.restart()

# Cleanup
sys.stdout.pop_filter()

master.admin("box.schema.user.revoke('guest', 'replication')")
