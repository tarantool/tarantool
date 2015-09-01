import os
import glob
from lib.tarantool_server import TarantoolServer

# master server
master = server
master_id = master.get_param('server')['id']

master.admin("box.schema.user.grant('guest', 'replication')")
master.admin("space = box.schema.space.create('test', { id = 99999, engine = \"sophia\" })")
master.admin("index = space:create_index('primary', { type = 'tree'})")
master.admin('for k = 1, 123 do space:insert{k, k*k} end')
master.admin('box.snapshot()')
lsn = master.get_lsn(master_id)

print '-------------------------------------------------------------'
print 'replica JOIN'
print '-------------------------------------------------------------'

# replica server
replica = TarantoolServer(server.ini)
replica.script = 'replication/replica.lua'
replica.vardir = server.vardir #os.path.join(server.vardir,'replica')
replica.rpl_master = master
replica.deploy()
replica.wait_lsn(master_id, lsn)
replica.admin('box.space.test:select()')

replica.stop()
replica.cleanup(True)

# remove space
master.admin("space:drop()")
master.admin('box.snapshot()')
