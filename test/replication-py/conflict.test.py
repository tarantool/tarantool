from __future__ import print_function

from lib.tarantool_server import TarantoolServer
from time import sleep
import yaml

def check_replication(nodes, select_args=""):
    for node in nodes:
        node.admin("box.space.test:select{{{}}}".format(select_args))

master = server
master.admin("box.schema.user.grant('guest', 'replication')")

replica = TarantoolServer(server.ini)
replica.script = "replication-py/replica.lua"
replica.vardir = server.vardir
replica.rpl_master = master
replica.deploy()

def parallel_run(cmd1, cmd2, compare):
    print("parallel send: {}".format(cmd1))
    print("parallel send: {}".format(cmd2))
    master.admin.socket.sendall(cmd1.encode("utf-8") + b'\n')
    replica.admin.socket.sendall(cmd2.encode("utf-8") + b'\n')
    master.admin.socket.recv(2048)
    replica.admin.socket.recv(2048)

    # wait for status changing in tarantool
    master_status = yaml.safe_load(master.admin(
        "box.info().replication[2].upstream.status", silent=True
    ))[0]
    replica_status = yaml.safe_load(replica.admin(
        "box.info().replication[1].upstream.status", silent=True
    ))[0]

    # wait for status
    results = [f(master_status, replica_status) for f in compare]
    while True:
        sleep(0.01)
        if any(results):
            print("replication state is correct")
            break

def prepare_cluster():
    print("reset master-master replication")
    master.stop()
    master.cleanup()
    master.start()
    master.admin("box.schema.user.grant('guest', 'replication')", silent=True)

    replica.stop()
    replica.cleanup()
    replica.start()

    master.admin("box.cfg{{replication='{}'}}".format(replica.iproto.uri), silent=True)
    r1_id = replica.get_param("id")
    r2_id = master.get_param("id")

    master.admin("space = box.schema.space.create('test')", silent=True)
    master.admin("index = space:create_index('primary', { type = 'tree'})", silent=True)
    master.admin("for k = 1, 9 do space:insert{k, k*k} end", silent=True)

    # wait lsn
    replica.wait_lsn(r2_id, master.get_lsn(r2_id))
    master.wait_lsn(r1_id, replica.get_lsn(r1_id))

# test1: double update in master and replica
prepare_cluster()
parallel_run(
    "box.space.test:update(1, {{'#', 2, 1}})",
    "box.space.test:update(1, {{'#', 2, 1}})",
    [
        lambda x,y: x == "stopped" or y == "stopped",
        lambda x,y: x == "follow" and y == "follow",
    ]
)
check_replication([master, replica], "1")

# test2: insert different values with single id
prepare_cluster()
parallel_run(
    "box.space.test:insert{20, 1}",
    "box.space.test:insert{20, 2}",
    [
        lambda x,y: x == "stopped" or y == "stopped",
        lambda x,y: x == "follow" and y == "follow",
    ]
)

# test3: update different values
prepare_cluster()
parallel_run(
    "box.space.test:update(2, {{'=', 2, 1}})",
    "box.space.test:update(2, {{'=', 2, 2}})",
    [lambda x,y: x == "follow" and y == "follow",]
)

# test4: CRDT increment with update
prepare_cluster()
parallel_run(
    "box.space.test:update(1, {{'+', 2, 1}})",
    "box.space.test:update(1, {{'+', 2, 2}})",
    [lambda x,y: x == "follow" and y == "follow",]
)
check_replication([master, replica], "1")

# test5: delete not existing key
prepare_cluster()
parallel_run(
    "box.space.test:delete(999)",
    "box.space.test:delete(999)",
    [lambda x,y: x == "follow" and y == "follow",]
)
check_replication([master, replica])

# cleanup
replica.stop()
replica.cleanup()
server.stop()
server.cleanup()
server.deploy()
