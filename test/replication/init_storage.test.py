import os
import glob
from lib.tarantool_server import TarantoolServer

# master server
master = server
master_id = master.get_param('server')['id']

master.admin("box.schema.user.grant('guest', 'read,write,execute', 'universe')")
master.admin("space = box.schema.create_space('test', {id =  42})")
master.admin("space:create_index('primary', { type = 'tree'})")

master.admin('for k = 1, 9 do space:insert{k, k*k} end')

for k in glob.glob(os.path.join(master.vardir, '*.xlog')):
    os.unlink(k)

print '-------------------------------------------------------------'
print 'replica test 1 (no such space)'
print '-------------------------------------------------------------'

replica = TarantoolServer(server.ini)
replica.script = 'replication/replica.lua'
replica.vardir = os.path.join(server.vardir, 'replica')
replica.rpl_master = master
replica.deploy()

replica.admin('box.space.test')

replica.stop()
replica.cleanup(True)

print '-------------------------------------------------------------'
print 'replica JOIN'
print '-------------------------------------------------------------'

master.admin('box.snapshot()')
master.restart()

replica.deploy()
replica.wait_lsn(master_id, master.get_lsn(master_id))
replica.admin('box.space.test:select()')

#
# gh-484: JOIN doesn't save data to snapshot with TREE index
#

replica.restart()

replica.admin('box.space.test:select()')
replica.stop()
replica.cleanup(True)

print '-------------------------------------------------------------'
print 'replica test 2 (must be ok)'
print '-------------------------------------------------------------'

master.restart()
master.admin('for k = 10, 19 do box.space[42]:insert{k, k*k*k} end')
lsn = master.get_lsn(master_id)

replica = TarantoolServer(server.ini)
replica.script = 'replication/replica.lua'
replica.vardir = os.path.join(server.vardir, 'replica')
replica.rpl_master = master
replica.deploy()

replica.admin('space = box.space.test');
replica.wait_lsn(master_id, lsn)
for i in range(1, 20):
    replica.admin('space:get{%d}' % i)

replica.stop()
replica.cleanup(True)

print '-------------------------------------------------------------'
print 'reconnect on JOIN/SUBSCRIBE'
print '-------------------------------------------------------------'

server.stop()
replica = TarantoolServer(server.ini)
replica.script = 'replication/replica.lua'
replica.vardir = os.path.join(server.vardir, 'replica')
replica.rpl_master = master
replica.deploy(wait=False)

print 'waiting reconnect on JOIN...'
server.start()
replica.wait_until_started()
print 'ok'

replica.stop()
server.stop()

print 'waiting reconnect on SUBSCRIBE...'
replica.start(wait=False)
server.start()
replica.wait_until_started()
print 'ok'

replica.stop()
replica.cleanup(True)

server.stop()
server.deploy()
