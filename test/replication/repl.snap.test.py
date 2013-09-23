import os
import glob
from lib.tarantool_server import TarantoolServer

# master server
master = server

master.admin('space = box.schema.create_space(\'test\', {id =  42})')
master.admin('space:create_index(\'primary\', \'hash\', {parts = { 0, \'num\' } })')

master.admin('for k = 1, 9 do space:insert(k, k*k) end')

for k in glob.glob(os.path.join(master.vardir, '*.xlog')):
	os.unlink(k)

# replica test 1
replica = TarantoolServer()
replica.deploy("replication/cfg/replica.cfg",
               replica.find_exe(self.args.builddir),
               os.path.join(self.args.vardir, "replica"))

for i in range(1, 10):
	replica.sql('select * from t42 where k0 = %d' % i)

replica.stop()
replica.cleanup(True)

master.admin('box.snapshot()')
master.restart()
master.admin('for k = 10, 19 do box.insert(42, k, k*k*k) end')

# replica test 2
replica = TarantoolServer()
replica.deploy("replication/cfg/replica.cfg",
               replica.find_exe(self.args.builddir),
               os.path.join(self.args.vardir, "replica"))

for i in range(1, 20):
	replica.sql('select * from t42 where k0 = %d' % i)

replica.stop()
replica.cleanup(True)

server.stop()
server.deploy(self.suite_ini["config"])

