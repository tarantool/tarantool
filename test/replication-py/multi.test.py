import sys
import os
from lib.tarantool_server import TarantoolServer
import yaml

REPLICA_N = 3
ROW_N = REPLICA_N * 20

##

# master server
master = server
master.admin("fiber = require('fiber')")
master.admin("box.schema.user.grant('guest', 'replication')")
master.admin("box.schema.user.grant('guest', 'execute', 'universe')")

print '----------------------------------------------------------------------'
print 'Bootstrap replicas'
print '----------------------------------------------------------------------'

# Start replicas
master.id = master.get_param('server')['id']
master_lsn = master.get_lsn(master.id)
cluster = [ master ]
for i in range(REPLICA_N - 1):
    server = TarantoolServer(server.ini)
    server.script = 'replication/replica.lua'
    server.vardir = os.path.join(server.vardir, 'replica', str(master.id + i))
    server.rpl_master = master
    server.deploy()
    # Wait replica to fully bootstrap.
    # Otherwise can get ACCESS_DENIED error.
    server.wait_lsn(master.id, master_lsn)
    cluster.append(server)

# Make a list of servers
sources = []
for server in cluster:
    sources.append(yaml.load(server.admin('box.cfg.listen', silent = True))[0])
    server.id = server.get_param('server')['id']

print 'done'

print '----------------------------------------------------------------------'
print 'Make a full mesh'
print '----------------------------------------------------------------------'

# Connect each server to each other to make full mesh
for server in cluster:
    server.iproto.py_con.eval("box.cfg { replication_source = ... }", [sources])

# Wait connections to establish
for server in cluster:
    for server2 in cluster:
        server.iproto.py_con.eval("""
            while #box.info.vclock[...] ~= nil do
                fiber.sleep(0.01)
            end;""", server2.id)
        print 'server', server.id, "connected"
    server.admin("box.info.vclock")

print 'done'

print '----------------------------------------------------------------------'
print 'Test inserts'
print '----------------------------------------------------------------------'

print 'Create a test space'
master.admin("_ = box.schema.space.create('test')")
master.admin("_ = box.space.test:create_index('primary')")
master_lsn = master.get_lsn(master.id)
# Wait changes to propagate to replicas
for server in cluster:
    server.wait_lsn(master.id, master_lsn)
    print 'server', server.id, 'is ok'
print

print 'Insert records'
for i in range(ROW_N):
    server = cluster[i % REPLICA_N]
    server.admin("box.space.test:insert{%d, %s}" % (i, server.id), silent = True)
print 'inserted %d records' % ROW_N
print

print 'Synchronize'
for server1 in cluster:
    for server2 in cluster:
        server1.wait_lsn(server2.id, server2.get_lsn(server2.id))
    print 'server', server.id, 'done'
print 'done'
print

print 'Check data'
for server in cluster:
    cnt = yaml.load(server.admin("box.space.test:len()", silent = True))[0]
    print 'server', server.id, 'is', cnt == ROW_N and 'ok' or 'not ok'
print 'Done'
print

print
print '----------------------------------------------------------------------'
print 'Cleanup'
print '----------------------------------------------------------------------'

for server in cluster:
    server.stop()
    print 'server', server.id, 'done'
print

master.cleanup()
master.deploy()
