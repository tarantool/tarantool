import os
from glob import iglob as glob
from lib.tarantool_server import TarantoolServer

# master server
master = server
master_id = master.get_param('server')['id']

master.admin("box.schema.user.grant('guest', 'replication')")

replica = TarantoolServer(server.ini)
replica.script = 'replication/replica.lua'
replica.vardir = server.vardir #os.path.join(server.vardir, 'replica')
replica.rpl_master = master
replica.deploy()
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica_id = replica.get_param('server')['id']
replica.admin('box.info.server.id')
replica.admin('box.info.server.ro')
replica.admin('box.info.server.lsn')
replica.stop()

print '-------------------------------------------------------------'
print 'replica is read-only until receive self server_id in _cluster'
print '-------------------------------------------------------------'

# Remove xlog retrived by SUBSCRIBE
filename = str(0).zfill(20) + ".xlog"
wal = os.path.join(os.path.join(replica.vardir, replica.name), filename)
os.remove(wal)

# Start replica without master
server.stop()
replica.rpl_master = None
os.putenv("MASTER", "") # clear information about MASTER from env
replica.start()

# Check that replica in read-only mode
replica.admin('box.info.server.id')
replica.admin('box.info.server.ro')
replica.admin('box.info.server.lsn')
replica.admin('space = box.schema.space.create("ro")')
replica.admin('box.info.vclock[%d]' % replica_id)

replica.stop()
replica.cleanup(True)
server.deploy()
