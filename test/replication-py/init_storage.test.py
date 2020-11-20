from __future__ import print_function

import os
import glob
from lib.tarantool_server import TarantoolStartError
from lib.tarantool_server import TarantoolServer

# master server
master = server
master_id = master.get_param("id")
master.admin("box.schema.user.grant('guest', 'replication')")

print("-------------------------------------------------------------")
print("gh-484: JOIN doesn't save data to snapshot with TREE index")
print("-------------------------------------------------------------")

master.admin("space = box.schema.space.create('test', {id =  42})")
master.admin("index = space:create_index('primary', { type = 'tree'})")

master.admin("for k = 1, 9 do space:insert{k, k*k} end")

replica = TarantoolServer(server.ini)
replica.script = "replication-py/replica.lua"
replica.vardir = server.vardir #os.path.join(server.vardir, 'replica')
replica.rpl_master = master
replica.deploy()
replica.admin("box.space.test:select()")

replica.restart()
replica.admin("box.space.test:select()")
replica.stop()
replica.cleanup()

print("-------------------------------------------------------------")
print("replica test 2 (must be ok)")
print("-------------------------------------------------------------")

master.restart()
master.admin("for k = 10, 19 do box.space[42]:insert{k, k*k*k} end")
master.admin("for k = 20, 29 do box.space[42]:upsert({k}, {}) end")
lsn = master.get_lsn(master_id)

replica = TarantoolServer(server.ini)
replica.script = "replication-py/replica.lua"
replica.vardir = server.vardir #os.path.join(server.vardir, 'replica')
replica.rpl_master = master
replica.deploy()

replica.admin("space = box.space.test");
replica.wait_lsn(master_id, lsn)
for i in range(1, 20):
    replica.admin("space:get{{{}}}".format(i))

replica.stop()
replica.cleanup()

print("-------------------------------------------------------------")
print("reconnect on JOIN/SUBSCRIBE")
print("-------------------------------------------------------------")

server.stop()
replica = TarantoolServer(server.ini)
replica.script = "replication-py/replica.lua"
replica.vardir = server.vardir #os.path.join(server.vardir, 'replica')
replica.rpl_master = master
replica.deploy(wait=False)

print("waiting reconnect on JOIN...")
server.start()
try:
    # Replica may fail to start due connection issues may occur, check
    # gh-4949. Also the test should have the ability to be restarted by
    # test-run using fragile list and in this way 'crash_expected' flag
    # should be enabled to let the test fail with exception.
    replica.crash_expected = True
    replica.wait_until_started()
except TarantoolStartError:
    print("not ok - server failed to start")
else:
    print("ok")

replica.stop()
server.stop()

print("waiting reconnect on SUBSCRIBE...")
replica.start(wait=False)
server.start()
try:
    # Replica may fail to start due connection issues may occur, check
    # gh-4949. Also the test should have the ability to be restarted by
    # test-run using fragile list and in this way 'crash_expected' flag
    # should be enabled to let the test fail with exception.
    replica.crash_expected = True
    replica.wait_until_started()
except TarantoolStartError:
    print("not ok - server failed to start")
else:
    print("ok")

replica.stop()
replica.cleanup()

server.stop()
server.deploy()
