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

server.iproto.reconnect() # re-connect with new permissions
print '-------------------------------------------------------------'
print ' gh-696: Check global READ permissions for replication'
print '-------------------------------------------------------------'


# Generate replica cluster UUID
replica_uuid = str(uuid.uuid4())

## Universal read permission is required to perform JOIN/SUBSCRIBE
rows = list(server.iproto.py_con.join(replica_uuid))
print len(rows) == 1 and rows[0].return_message.find('Read access') >= 0 and \
    'ok' or 'not ok', '-', 'join without read permissions on universe'
rows = list(server.iproto.py_con.subscribe(cluster_uuid, replica_uuid))
print len(rows) == 1 and rows[0].return_message.find('Read access') >= 0 and \
    'ok' or 'not ok', '-', 'subscribe without read permissions on universe'
## Write permission to space `_cluster` is required to perform JOIN
server.admin("box.schema.user.grant('guest', 'read', 'universe')")
server.iproto.reconnect() # re-connect with new permissions
rows = list(server.iproto.py_con.join(replica_uuid))
print len(rows) == 1 and rows[0].return_message.find('Write access') >= 0 and \
    'ok' or 'not ok', '-', 'join without write permissions to _cluster'

def check_join(msg):
    ok = True
    for resp in server.iproto.py_con.join(replica_uuid):
        if resp.completion_status != 0:
            print 'not ok', '-', msg, resp.return_message
            ok = False

    server.iproto.reconnect() # the only way to stop JOIN
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
server.iproto.reconnect() # re-connect with new permissions
server_id = check_join('join with granted permissions')
server.iproto.py_con.space('_cluster').delete(server_id)

# JOIN with granted role
server.admin("box.schema.user.revoke('guest', 'read', 'universe')")
server.admin("box.schema.user.revoke('guest', 'write', 'space', '_cluster')")
server.admin("box.schema.user.grant('guest', 'replication')")
server.iproto.reconnect() # re-connect with new permissions
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
print len(rows) > 0 and rows[-1].return_message.find('.snap') >= 0 and \
    'ok' or 'not ok', '-', 'join without snapshots'
res = server.iproto.py_con.space('_cluster').select(())
if server_count <= len(res):
    print 'ok - _cluster did not change after unsuccessful JOIN'
else:
    print 'not ok - _cluster did change after unsuccessful JOIN'
    print res

server.admin("box.schema.user.revoke('guest', 'replication')")
server.admin('box.snapshot()')

print '-------------------------------------------------------------'
print 'gh-434: Assertion if replace _cluster tuple for local server'
print '-------------------------------------------------------------'

master_uuid = server.get_param('server')['uuid']
sys.stdout.push_filter(master_uuid, '<master uuid>')

# Invalid UUID
server.admin("box.space._cluster:replace{1, require('uuid').NULL:str()}")

# Update of UUID is not OK
server.admin("box.space._cluster:replace{1, require('uuid').str()}")

# Update of tail is OK
server.admin("box.space._cluster:update(1, {{'=', 3, 'test'}})")

print '-------------------------------------------------------------'
print 'gh-1140: Assertion if replace _cluster tuple for remote server'
print '-------------------------------------------------------------'

# Test that insert is OK
new_uuid = '0d5bd431-7f3e-4695-a5c2-82de0a9cbc95'
server.admin("box.space._cluster:insert{{5, '{0}'}}".format(new_uuid))
server.admin("box.info.vclock[5] == nil")

# Replace with the same UUID is OK
server.admin("box.space._cluster:replace{{5, '{0}'}}".format(new_uuid))
# Replace with a new UUID is not OK
new_uuid = 'a48a19a3-26c0-4f8c-a5b5-77377bab389b'
server.admin("box.space._cluster:replace{{5, '{0}'}}".format(new_uuid))
# Update of tail is OK
server.admin("box.space._cluster:update(5, {{'=', 3, 'test'}})")
# Delete is OK
server.admin("box.space._cluster:delete(5)")
# gh-1219: LSN must not be removed from vclock on unregister
server.admin("box.info.vclock[5] == nil")

# Cleanup
server.stop()
server.deploy()

print '-------------------------------------------------------------'
print 'Start a new replica and check box.info on the start'
print '-------------------------------------------------------------'
# master server
master = server
master_id = master.get_param('server')['id']

master.admin("box.schema.user.grant('guest', 'replication')")

replica = TarantoolServer(server.ini)
replica.script = 'replication-py/replica.lua'
replica.vardir = server.vardir
replica.rpl_master = master
replica.deploy()
replica_id = replica.get_param('server')['id']
replica_uuid = replica.get_param('server')['uuid']
sys.stdout.push_filter(replica_uuid, '<replica uuid>')

replica.admin('box.info.server.id == %d' % replica_id)
replica.admin('not box.info.server.ro')
replica.admin('box.info.server.lsn == 0')
replica.admin('box.info.vclock[%d] == nil' % replica_id)

print '-------------------------------------------------------------'
print 'Modify data to change LSN and check box.info'
print '-------------------------------------------------------------'
replica.admin('box.space._schema:insert{"test", 48}')
replica.admin('box.info.server.lsn == 1')
replica.admin('box.info.vclock[%d] == 1' % replica_id)

print '-------------------------------------------------------------'
print 'Unregister replica and check box.info'
print '-------------------------------------------------------------'
# gh-527: update vclock on delete from box.space._cluster'
master.admin('box.space._cluster:delete{%d} ~= nil' % replica_id)
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica.admin('box.info.server.id ~= %d' % replica_id)
replica.admin('box.info.server.lsn == -1')
# gh-1219: LSN must not be removed from vclock on unregister
replica.admin('box.info.vclock[%d] == 1' % replica_id)
# gh-246: box.info.server.ro is controlled by box.cfg { read_only = xx }
# unregistration doesn't change box.info.server.ro
replica.admin('not box.info.server.ro')
# actually box is read-only if id is not registered
replica.admin('box.space._schema:replace{"test", 48}')
replica.admin('box.cfg { read_only = true }')
replica.admin('box.space._schema:replace{"test", 48}')
replica.admin('box.cfg { read_only = false }')
replica.admin('box.space._schema:replace{"test", 48}')

print '-------------------------------------------------------------'
print 'Re-register replica with the same server_id'
print '-------------------------------------------------------------'

replica.admin('box.cfg { read_only = true }')
master.admin('box.space._cluster:insert{%d, "%s"} ~= nil' %
    (replica_id, replica_uuid))
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica.admin('box.info.server.id == %d' % replica_id)
# gh-1219: LSN must not be removed from vclock on unregister
replica.admin('box.info.server.lsn == 1')
replica.admin('box.info.vclock[%d] == 1' % replica_id)

# gh-246: box.info.server.ro is controlled by box.cfg { read_only = xx  }
# registration doesn't change box.info.server.ro
replica.admin('box.info.server.ro == true')
# is ro
replica.admin('box.space._schema:replace{"test", 48}')
replica.admin('box.cfg { read_only = false }')
# is not ro
#replica.admin('box.space._schema:replace{"test", 48}')

print '-------------------------------------------------------------'
print 'Re-register replica with a new server_id'
print '-------------------------------------------------------------'
master.admin('box.space._cluster:delete{%d} ~= nil' % replica_id)
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica_id2 = 10
master.admin('box.space._cluster:insert{%d, "%s"} ~= nil' %
    (replica_id2, replica_uuid))
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica.admin('box.info.server.id == %d' % replica_id2)
replica.admin('not box.info.server.ro')
replica.admin('box.info.server.lsn == 0')
replica.admin('box.info.vclock[%d] == 1' % replica_id)
replica.admin('box.info.vclock[%d] == nil' % replica_id2)

print '-------------------------------------------------------------'
print 'Check that server_id can\'t be changed by UPDATE'
print '-------------------------------------------------------------'
replica_id3 = 11
server.admin("box.space._cluster:update(%d, {{'=', 1, %d}})" %
    (replica_id2, replica_id3))
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica.admin('box.info.server.id == %d' % replica_id2)
replica.admin('not box.info.server.ro')
replica.admin('box.info.server.lsn == 0')
replica.admin('box.info.vclock[%d] == 1' % replica_id)
replica.admin('box.info.vclock[%d] == nil' % replica_id2)
replica.admin('box.info.vclock[%d] == nil' % replica_id3)

print '-------------------------------------------------------------'
print 'Unregister replica and check box.info (second attempt)'
print '-------------------------------------------------------------'
# gh-527: update vclock on delete from box.space._cluster'
master.admin('box.space._cluster:delete{%d} ~= nil' % replica_id2)
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica.admin('box.info.server.id ~= %d' % replica_id)
# Backward-compatibility: box.info.server.lsn is -1 instead of nil
replica.admin('box.info.server.lsn == -1')
replica.admin('box.info.vclock[%d] == nil' % replica_id2)

print '-------------------------------------------------------------'
print 'JOIN replica to read-only master'
print '-------------------------------------------------------------'

#gh-1230 Assertion vclock_has on attempt to JOIN read-only master
failed = TarantoolServer(server.ini)
failed.script = 'replication-py/failed.lua'
failed.vardir = server.vardir
failed.rpl_master = replica
failed.name = "failed"
try:
    failed.deploy()
except Exception as e:
    line = "ER_READONLY"
    if failed.logfile_pos.seek_once(line) >= 0:
        print "'%s' exists in server log" % line

print '-------------------------------------------------------------'
print 'Sync master with replica'
print '-------------------------------------------------------------'

# Sync master with replica
replication_source = yaml.load(replica.admin('box.cfg.listen', silent = True))[0]
sys.stdout.push_filter(replication_source, '<replication_source>')
master.admin("box.cfg{ replication_source = '%s' }" % replication_source)

master.wait_lsn(replica_id, replica.get_lsn(replica_id))
master.admin('box.info.vclock[%d] == 1' % replica_id)
master.admin('box.info.vclock[%d] == nil' % replica_id2)
master.admin('box.info.vclock[%d] == nil' % replica_id3)

master.admin("box.cfg{ replication_source = '' }")
replica.stop()
replica.cleanup(True)

print '-------------------------------------------------------------'
print 'Start a new replica and check that server_id, LSN is re-used'
print '-------------------------------------------------------------'

#
# gh-1219: Proper removal of servers with non-zero LSN from _cluster
#
# Snapshot is required. Otherwise a relay will skip records made by previous
# replica with the re-used id.
master.admin("box.snapshot()")
master.admin('box.info.vclock[%d] == 1' % replica_id)

replica = TarantoolServer(server.ini)
replica.script = 'replication-py/replica.lua'
replica.vardir = server.vardir
replica.rpl_master = master
replica.deploy()
replica.wait_lsn(master_id, master.get_lsn(master_id))
# Check that replica_id was re-used
replica.admin('box.info.server.id == %d' % replica_id)
replica.admin('not box.info.server.ro')
# All records were succesfully recovered.
# Replica should have the same vclock as master.
master.admin('box.info.vclock[%d] == 1' % replica_id)
replica.admin('box.info.vclock[%d] == 1' % replica_id)
master.admin('box.info.vclock[%d] == nil' % replica_id2)
replica.admin('box.info.vclock[%d] == nil' % replica_id2)
master.admin('box.info.vclock[%d] == nil' % replica_id3)
replica.admin('box.info.vclock[%d] == nil' % replica_id3)

print '-------------------------------------------------------------'
print 'Cleanup'
print '-------------------------------------------------------------'

replica.stop()
replica.cleanup(True)

# Cleanup
sys.stdout.pop_filter()

master.admin("box.schema.user.revoke('guest', 'replication')")
