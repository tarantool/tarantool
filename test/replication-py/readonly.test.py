import os
from glob import iglob as glob
from lib.tarantool_server import TarantoolServer

# master server
master = server
master_id = master.get_param('server')['id']

master.admin("box.schema.user.grant('guest', 'replication')")

replica = TarantoolServer(server.ini)
replica.script = 'replication-py/replica.lua'
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

# #1075: Box.once should wait before the server enters RW mode
#
# We expect the replica to get blocked in box.cfg{}, hence wait = False.
# Since neither xlog files nor master are available, the replica waits
# indefinitely.
#
# Note: replica monitors _cluster table, synchronized via replication.
# The replica enters RW mode once it discovers that according to
# _cluster table it had joined the cluster. Never happens in this
# particular test case.
replica.rpl_master = None
os.putenv("MASTER", "") # clear information about MASTER from env
replica.start(wait = False)

# Check that replica in read-only mode
replica.admin('box.info.server.id')
replica.admin('box.info.server.ro')
replica.admin('box.info.server.lsn')
replica.admin('space = box.schema.space.create("ro")')
replica.admin('box.info.vclock[%d]' % replica_id)

# Check that box.cfg didn't return yet
replica.admin('box_cfg_done')

replica.stop()
replica.cleanup(True)
server.deploy()
