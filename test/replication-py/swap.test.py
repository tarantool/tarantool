from __future__ import print_function

import os
import tarantool
from lib.tarantool_server import TarantoolServer
import re
import yaml

REPEAT = 20
ID_BEGIN = 0
ID_STEP = 5
LOGIN = "test"
PASSWORD = "pass123456"

engines = ["memtx", "vinyl"]

def insert_tuples(_server, begin, end, msg = "tuple"):
    for engine in engines:
        for i in range(begin, end):
            print("box.space.{}:insert{{{}, \"{} {}\"}}".format(engine, i, msg, i))
            print("-")
            space = _server.iproto.py_con.space(engine)
            print(space.insert((i, "{} {}".format(msg, i))))

def select_tuples(_server, begin, end):
    for engine in engines:
        for i in range(begin, end):
            print("box.space.{}:select{{{}}}".format(engine, i))
            print("-")
            space = _server.iproto.py_con.space(engine)
            print(space.select(i))

# master server
master = server
# Re-deploy server to cleanup Phia data
master.stop()
master.cleanup()
master.deploy()
master.admin("box.schema.user.create('{}', {{ password = '{}'}})".format(LOGIN, PASSWORD))
master.admin("box.schema.user.grant('{}', 'read,write,execute', 'universe')".format(LOGIN))
master.iproto.py_con.authenticate(LOGIN, PASSWORD)
master.uri = "{}:{}@{}".format(LOGIN, PASSWORD, master.iproto.uri)
os.putenv("MASTER", master.uri)

# replica server
replica = TarantoolServer()
replica.script = "replication-py/replica.lua"
replica.vardir = server.vardir
replica.deploy()
replica.admin("while box.info.id == 0 do require('fiber').sleep(0.01) end")
replica.uri = "{}:{}@{}".format(LOGIN, PASSWORD, replica.iproto.uri)
replica.admin("while box.space['_priv']:len() < 1 do require('fiber').sleep(0.01) end")
replica.iproto.py_con.authenticate(LOGIN, PASSWORD)

for engine in engines:
    master.admin("s = box.schema.space.create('{}', {{ engine = '{}'}})".format(engine, engine))
    master.admin("index = s:create_index('primary', {type = 'tree'})")

master_id = master.get_param("id")
replica_id = replica.get_param("id")

id = ID_BEGIN
for i in range(REPEAT):
    print("test {} iteration".format(i))

    # insert to master
    insert_tuples(master, id, id + ID_STEP)
    # select from replica
    replica.wait_lsn(master_id, master.get_lsn(master_id))
    select_tuples(replica, id, id + ID_STEP)
    id += ID_STEP

    # insert to master
    insert_tuples(master, id, id + ID_STEP)
    # select from replica
    replica.wait_lsn(master_id, master.get_lsn(master_id))
    select_tuples(replica, id, id + ID_STEP)
    id += ID_STEP

    print("swap servers")
    # reconfigure replica to master
    replica.rpl_master = None
    print("switch replica to master")
    replica.admin("box.cfg{replication=''}")
    # reconfigure master to replica
    master.rpl_master = replica
    print("switch master to replica")
    master.admin("box.cfg{{replication='{}'}}".format(replica.uri), silent=True)

    # insert to replica
    insert_tuples(replica, id, id + ID_STEP)
    # select from master
    master.wait_lsn(replica_id, replica.get_lsn(replica_id))
    select_tuples(master, id, id + ID_STEP)
    id += ID_STEP

    # insert to replica
    insert_tuples(replica, id, id + ID_STEP)
    # select from master
    master.wait_lsn(replica_id, replica.get_lsn(replica_id))
    select_tuples(master, id, id + ID_STEP)
    id += ID_STEP

    print("rollback servers configuration")
    # reconfigure replica to master
    master.rpl_master = None
    print("switch master to master")
    master.admin("box.cfg{replication=''}")
    # reconfigure master to replica
    replica.rpl_master = master
    print("switch replica to replica")
    replica.admin("box.cfg{{replication='{}'}}".format(master.uri), silent=True)


# Cleanup.
replica.stop()
replica.cleanup()
server.stop()
server.deploy()
