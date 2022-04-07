from __future__ import print_function

import os
import sys
import re
import yaml
import uuid
import glob
from lib.tarantool_server import TarantoolServer

## Get cluster uuid
cluster_uuid = ""
try:
    cluster_uuid = yaml.safe_load(server.admin("box.space._schema:get('cluster')",
        silent = True))[0][1]
    uuid.UUID("{" + cluster_uuid + "}")
    print("ok - cluster uuid")
except Exception as e:
    print("not ok - invalid cluster uuid", e)

server.iproto.reconnect() # re-connect with new permissions
print("-------------------------------------------------------------")
print(" gh-696: Check global READ permissions for replication")
print("-------------------------------------------------------------")


# Generate replica cluster UUID
replica_uuid = str(uuid.uuid4())

## Universal read permission is required to perform JOIN/SUBSCRIBE
rows = list(server.iproto.py_con.join(replica_uuid))
status = len(rows) == 1 and rows[0].return_message.find("Read access") >= 0 and \
    "ok" or "not ok"
print("{} - join without read permissions on universe".format(status))
rows = list(server.iproto.py_con.subscribe(cluster_uuid, replica_uuid))
status = len(rows) == 1 and rows[0].return_message.find("Read access") >= 0 and \
    "ok" or "not ok"
print("{} - subscribe without read permissions on universe".format(status))
## Write permission to space `_cluster` is required to perform JOIN
server.admin("box.schema.user.grant('guest', 'read', 'universe')")
server.iproto.reconnect() # re-connect with new permissions
rows = list(server.iproto.py_con.join(replica_uuid))
status = len(rows) == 1 and rows[0].return_message.find("Write access") >= 0 and \
    "ok" or "not ok"
print("{} - join without write permissions to _cluster".format(status))

def check_join(msg):
    ok = True
    for resp in server.iproto.py_con.join(replica_uuid):
        if resp._return_code != 0:
            print("not ok - {} {}".format(msg, resp.return_message))
            ok = False

    server.iproto.reconnect() # the only way to stop JOIN
    if not ok:
        return
    tuples = server.iproto.py_con.space("_cluster").select(replica_uuid, index = 1)
    if len(tuples) == 0:
        print("not ok - {} missing entry in _cluster".format(msg))
        return
    server_id = tuples[0][0]
    print("ok - {}".format(msg))
    return server_id

## JOIN with permissions
server.admin("box.schema.user.grant('guest', 'write', 'space', '_cluster')")
server.iproto.reconnect() # re-connect with new permissions
server_id = check_join("join with granted permissions")
server.iproto.py_con.space("_cluster").delete(server_id)

# JOIN with granted role
server.admin("box.schema.user.revoke('guest', 'read', 'universe')")
server.admin("box.schema.user.revoke('guest', 'write', 'space', '_cluster')")
server.admin("box.schema.user.grant('guest', 'replication')")
server.iproto.reconnect() # re-connect with new permissions
server_id = check_join("join with granted role")
server.iproto.py_con.space("_cluster").delete(server_id)

print("-------------------------------------------------------------")
print("gh-434: Assertion if replace _cluster tuple for local server")
print("-------------------------------------------------------------")

master_uuid = server.get_param("uuid")
sys.stdout.push_filter(master_uuid, "<master uuid>")

# Invalid UUID
server.admin("box.space._cluster:replace{1, require('uuid').NULL:str()}")

# Update of UUID is not OK
server.admin("box.space._cluster:replace{1, require('uuid').str()}")

# Update of tail is OK
server.admin("box.space._cluster:update(1, {{'=', 3, 'test'}})")

print("-------------------------------------------------------------")
print("gh-1140: Assertion if replace _cluster tuple for remote server")
print("-------------------------------------------------------------")

# Test that insert is OK
new_uuid = "0d5bd431-7f3e-4695-a5c2-82de0a9cbc95"
server.admin("box.space._cluster:insert{{5, '{0}'}}".format(new_uuid))
server.admin("box.info.vclock[5] == nil")

# Replace with the same UUID is OK
server.admin("box.space._cluster:replace{{5, '{0}'}}".format(new_uuid))
# Replace with a new UUID is not OK
new_uuid = "a48a19a3-26c0-4f8c-a5b5-77377bab389b"
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

print("-------------------------------------------------------------")
print("Start a new replica and check box.info on the start")
print("-------------------------------------------------------------")
# master server
master = server
master_id = master.get_param("id")

master.admin("box.schema.user.grant('guest', 'replication')")

replica = TarantoolServer(server.ini)
replica.script = "replication-py/replica.lua"
replica.vardir = server.vardir
replica.rpl_master = master
replica.deploy()
replica_id = replica.get_param("id")
replica_uuid = replica.get_param("uuid")
sys.stdout.push_filter(replica_uuid, "<replica uuid>")

replica.admin("box.info.id == {}".format(replica_id))
replica.admin("not box.info.ro")
replica.admin("box.info.lsn == 0")
replica.admin("box.info.vclock[{}] == nil".format(replica_id))

print("-------------------------------------------------------------")
print("Modify data to bump LSN and check box.info")
print("-------------------------------------------------------------")
replica.admin("box.space._schema:insert{\"test\", 48}")
replica.admin("box.info.lsn == 1")
replica.admin("box.info.vclock[{}] == 1".format(replica_id))

print("-------------------------------------------------------------")
print("Connect master to replica")
print("-------------------------------------------------------------")
replication_source = yaml.safe_load(replica.admin("box.cfg.listen", silent = True))[0]
sys.stdout.push_filter(replication_source, "<replication_source>")
master.admin("box.cfg{{ replication_source = '{}' }}".format(replication_source))
master.wait_lsn(replica_id, replica.get_lsn(replica_id))

print("-------------------------------------------------------------")
print("Disconnect replica from master")
print("-------------------------------------------------------------")
replica.admin("box.cfg { replication_source = \"\" }")

print("-------------------------------------------------------------")
print("Unregister replica")
print("-------------------------------------------------------------")

master.admin("box.space._cluster:delete{{{}}} ~= nil".format(replica_id))

# gh-1219: LSN must not be removed from vclock on unregister
master.admin("box.info.vclock[{}] == 1".format(replica_id))

print("-------------------------------------------------------------")
print("Modify data to bump LSN on replica")
print("-------------------------------------------------------------")
replica.admin("box.space._schema:insert{\"tost\", 49}")
replica.admin("box.info.lsn == 2")
replica.admin("box.info.vclock[{}] == 2".format(replica_id))

print("-------------------------------------------------------------")
print("Master must not crash then receives orphan rows from replica")
print("-------------------------------------------------------------")

replication_source = yaml.safe_load(replica.admin("box.cfg.listen", silent = True))[0]
sys.stdout.push_filter(replication_source, "<replication>")
master.admin("box.cfg{{ replication = '{}' }}".format(replication_source))

master.wait_lsn(replica_id, replica.get_lsn(replica_id))
master.admin("box.info.vclock[{}] == 2".format(replica_id))

master.admin("box.cfg{ replication = '' }")
replica.stop()
replica.cleanup()

print("-------------------------------------------------------------")
print("Start a new replica and check that server_id, LSN is re-used")
print("-------------------------------------------------------------")

#
# gh-1219: Proper removal of servers with non-zero LSN from _cluster
#
# Snapshot is required. Otherwise a relay will skip records made by previous
# replica with the re-used id.
master.admin("box.snapshot()")
master.admin("box.info.vclock[{}] == 2".format(replica_id))

replica = TarantoolServer(server.ini)
replica.script = "replication-py/replica.lua"
replica.vardir = server.vardir
replica.rpl_master = master
replica.deploy()
replica.wait_lsn(master_id, master.get_lsn(master_id))
# Check that replica_id was re-used
replica.admin("box.info.id == {}".format(replica_id))
replica.admin("not box.info.ro")
# All records were succesfully recovered.
# Replica should have the same vclock as master.
master.admin("box.info.vclock[{}] == 2".format(replica_id))
replica.admin("box.info.vclock[{}] == 2".format(replica_id))

replica.stop()
replica.cleanup()
master.admin("box.space._cluster:delete{{{}}} ~= nil".format(replica_id))

print("-------------------------------------------------------------")
print("JOIN replica to read-only master")
print("-------------------------------------------------------------")

# master server
master = server
master.admin("box.cfg { read_only = true }")
#gh-1230 Assertion vclock_has on attempt to JOIN read-only master
failed = TarantoolServer(server.ini)
failed.script = "replication-py/failed.lua"
failed.vardir = server.vardir
failed.rpl_master = master
failed.name = "failed"

failed.deploy(True, wait=False)
line = "ER_READONLY"
if failed.logfile_pos.seek_wait(line):
    print("'{}' exists in server log".format(line))

failed.stop()
failed.cleanup()

master.admin("box.cfg { read_only = false }")

print("-------------------------------------------------------------")
print("JOIN replica with different replica set UUID")
print("-------------------------------------------------------------")

failed = TarantoolServer(server.ini)
failed.script = "replication-py/uuid_mismatch.lua"
failed.vardir = server.vardir
failed.rpl_master = master
failed.name = "uuid_mismatch"
failed.crash_expected = True
try:
    failed.deploy()
except Exception as e:
    line = "ER_REPLICASET_UUID_MISMATCH"
    if failed.logfile_pos.seek_once(line) >= 0:
        print("'{}' exists in server log".format(line))

failed.cleanup()

print("-------------------------------------------------------------")
print("Cleanup")
print("-------------------------------------------------------------")

# Cleanup
sys.stdout.pop_filter()
master.admin("box.schema.user.revoke('guest', 'replication')")
master.admin("box.space._cluster:delete{2} ~= nil")
