import os
import tarantool
from lib.tarantool_server import TarantoolServer

REPEAT = 20
ID_BEGIN = 0
ID_STEP = 5

def insert_tuples(_server, begin, end, msg = "tuple"):
    for i in range(begin, end):
        _server.sql("insert into t0 values (%d, '%s %d')" % (i, msg, i))

def select_tuples(_server, begin, end, lsn):
    _server.wait_lsn(lsn)
    for i in range(begin, end):
        _server.sql("select * from t0 where k0 = %d" % i)

# master server
master = server
cfgfile_bkp = server.cfgfile_source
# replica server
replica = TarantoolServer()
replica.rpl_master = master
replica.cfgfile_source = "replication/cfg/replica.cfg"
replica.vardir = os.path.join(server.vardir, 'replica')
replica.deploy()

schema = {
    0 : {
            'default_type': tarantool.STR,
            'fields' : {
                0 : tarantool.NUM,
                1 : tarantool.STR
            },
            'indexes': {
                0 : [0] # HASH
            }
    }
}

master.sql.set_schema(schema)
replica.sql.set_schema(schema)

master.admin("s = box.schema.create_space('tweedledum', {id = 0})")
master.admin("s:create_index('primary', {type = 'hash'})")
id = ID_BEGIN
for i in range(REPEAT):
    print "test %d iteration" % i

    # insert to master
    insert_tuples(master, id, id + ID_STEP)
    # select from replica
    select_tuples(replica, id, id + ID_STEP, master.get_param("lsn"))
    id += ID_STEP

    # insert to master
    insert_tuples(master, id, id + ID_STEP)
    # select from replica
    select_tuples(replica, id, id + ID_STEP, master.get_param("lsn"))
    id += ID_STEP

    print "swap servers"
    # reconfigure replica to master
    replica.rpl_master = None
    replica.reconfigure("replication/cfg/replica_to_master.cfg", silent = False)
    # reconfigure master to replica
    master.rpl_master = replica
    master.reconfigure("replication/cfg/master_to_replica.cfg", silent = False)

    # insert to replica
    insert_tuples(replica, id, id + ID_STEP)
    # select from master
    select_tuples(master, id, id + ID_STEP, replica.get_param("lsn"))
    id += ID_STEP

    # insert to replica
    insert_tuples(replica, id, id + ID_STEP)
    # select from master
    select_tuples(master, id, id + ID_STEP, replica.get_param("lsn"))
    id += ID_STEP

    print "rollback servers configuration"
    # reconfigure replica to master
    master.rpl_master = None
    master.reconfigure("replication/cfg/master.cfg", silent = False)
    # reconfigure master to replica
    replica.rpl_master = master
    replica.reconfigure("replication/cfg/replica.cfg", silent = False)


# Cleanup.
replica.stop()
replica.cleanup(True)
server.stop()
server.cfgfile_source = cfgfile_bkp
server.deploy()
