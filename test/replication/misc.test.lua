uuid = require('uuid')
test_run = require('test_run').new()

box.schema.user.grant('guest', 'replication')

-- gh-2991 - Tarantool asserts on box.cfg.replication update if one of
-- servers is dead
replication_timeout = box.cfg.replication_timeout
replication_connect_timeout = box.cfg.replication_connect_timeout
box.cfg{replication_timeout=0.05, replication_connect_timeout=0.05, replication={}}
box.cfg{replication_connect_quorum=2}
box.cfg{replication = {'127.0.0.1:12345', box.cfg.listen}}
box.info.status
box.info.ro

-- gh-3606 - Tarantool crashes if box.cfg.replication is updated concurrently
fiber = require('fiber')
c = fiber.channel(2)
f = function() fiber.create(function() pcall(box.cfg, {replication = {12345}}) c:put(true) end) end
f()
f()
c:get()
c:get()

box.cfg{replication = "", replication_timeout = replication_timeout, replication_connect_timeout = replication_connect_timeout}
box.info.status
box.info.ro

-- gh-3111 - Allow to rebootstrap a replica from a read-only master
replica_uuid = uuid.new()
test_run:cmd('create server test with rpl_master=default, script="replication/replica_uuid.lua"')
test_run:cmd(string.format('start server test with args="%s"', replica_uuid))
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
box.cfg{read_only = true}
test_run:cmd(string.format('start server test with args="%s"', replica_uuid))
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
box.cfg{read_only = false}
test_run:cmd('delete server test')
test_run:cleanup_cluster()

-- gh-3160 - Send heartbeats if there are changes from a remote master only
SERVERS = { 'autobootstrap1', 'autobootstrap2', 'autobootstrap3' }

-- Deploy a cluster.
test_run:create_cluster(SERVERS, "replication", {args="0.03"})
test_run:wait_fullmesh(SERVERS)
test_run:cmd("switch autobootstrap3")
test_run = require('test_run').new()
fiber = require('fiber')
_ = box.schema.space.create('test_timeout'):create_index('pk')
test_run:cmd("setopt delimiter ';'")
function wait_not_follow(replicaA, replicaB)
    return test_run:wait_cond(function()
        return replicaA.status ~= 'follow' or replicaB.status ~= 'follow'
    end, box.cfg.replication_timeout)
end;
function test_timeout()
    local replicaA = box.info.replication[1].upstream or box.info.replication[2].upstream
    local replicaB = box.info.replication[3].upstream or box.info.replication[2].upstream
    local follows = test_run:wait_cond(function()
        return replicaA.status == 'follow' or replicaB.status == 'follow'
    end)
    if not follows then error('replicas are not in the follow status') end
    for i = 0, 99 do
        box.space.test_timeout:replace({1})
        if wait_not_follow(replicaA, replicaB) then
            return error(box.info.replication)
        end
    end
    return true
end;
test_run:cmd("setopt delimiter ''");
test_timeout()

-- gh-3247 - Sequence-generated value is not replicated in case
-- the request was sent via iproto.
test_run:cmd("switch autobootstrap1")
net_box = require('net.box')
_ = box.schema.space.create('space1')
_ = box.schema.sequence.create('seq')
_ = box.space.space1:create_index('primary', {sequence = true} )
_ = box.space.space1:create_index('secondary', {parts = {2, 'unsigned'}})
box.schema.user.grant('guest', 'read,write', 'space', 'space1')
c = net_box.connect(box.cfg.listen)
c.space.space1:insert{box.NULL, "data"} -- fails, but bumps sequence value
c.space.space1:insert{box.NULL, 1, "data"}
box.space.space1:select{}
vclock = test_run:get_vclock("autobootstrap1")
vclock[0] = nil
_ = test_run:wait_vclock("autobootstrap2", vclock)
test_run:cmd("switch autobootstrap2")
box.space.space1:select{}
test_run:cmd("switch autobootstrap1")
box.space.space1:drop()

test_run:cmd("switch default")
test_run:drop_cluster(SERVERS)
test_run:cleanup_cluster()

-- gh-3642 - Check that socket file descriptor doesn't leak
-- when a replica is disconnected.
rlimit = require('rlimit')
lim = rlimit.limit()
rlimit.getrlimit(rlimit.RLIMIT_NOFILE, lim)
old_fno = lim.rlim_cur
lim.rlim_cur = 64
rlimit.setrlimit(rlimit.RLIMIT_NOFILE, lim)

test_run:cmd('create server sock with rpl_master=default, script="replication/replica.lua"')
test_run:cmd('start server sock')
test_run:cmd('switch sock')
test_run = require('test_run').new()
fiber = require('fiber')
test_run:cmd("setopt delimiter ';'")
for i = 1, 64 do
    local replication = box.cfg.replication
    box.cfg{replication = {}}
    box.cfg{replication = replication}
    while box.info.replication[1].upstream.status ~= 'follow' do
        fiber.sleep(0.001)
    end
end;
test_run:cmd("setopt delimiter ''");

box.info.replication[1].upstream.status

test_run:cmd('switch default')

lim.rlim_cur = old_fno
rlimit.setrlimit(rlimit.RLIMIT_NOFILE, lim)

test_run:cmd("stop server sock")
test_run:cmd("cleanup server sock")
test_run:cmd("delete server sock")
test_run:cleanup_cluster()

box.schema.user.revoke('guest', 'replication')

-- gh-3510 assertion failure in replica_on_applier_disconnect()
test_run:cmd('create server er_load1 with script="replication/er_load1.lua"')
test_run:cmd('create server er_load2 with script="replication/er_load2.lua"')
test_run:cmd('start server er_load1 with wait=False, wait_load=False')
-- Instance er_load2 will fail with error ER_REPLICASET_UUID_MISMATCH.
-- This is OK since we only test here that er_load1 doesn't assert.
test_run:cmd('start server er_load2 with wait=True, wait_load=True, crash_expected = True')
test_run:cmd('stop server er_load1')
-- er_load2 exits automatically.
test_run:cmd('cleanup server er_load1')
test_run:cmd('cleanup server er_load2')
test_run:cmd('delete server er_load1')
test_run:cmd('delete server er_load2')
test_run:cleanup_cluster()

--
-- Test case for gh-3637, gh-4550. Before the fix replica would
-- exit with an error if a user does not exist or a password is
-- incorrect. Now check that we don't hang/panic and successfully
-- connect.
--
fiber = require('fiber')
test_run:cmd("create server replica_auth with rpl_master=default, script='replication/replica_auth.lua'")
test_run:cmd("start server replica_auth with wait=False, wait_load=False, args='cluster:pass 0.05'")
-- Wait a bit to make sure replica waits till user is created.
fiber.sleep(0.1)
box.schema.user.create('cluster')
-- The user is created. Let the replica fail auth request due to
-- a wrong password.
fiber.sleep(0.1)
box.schema.user.passwd('cluster', 'pass')
box.schema.user.grant('cluster', 'replication')

while box.info.replication[2] == nil do fiber.sleep(0.01) end
vclock = test_run:get_vclock('default')
vclock[0] = nil
_ = test_run:wait_vclock('replica_auth', vclock)

test_run:cmd("stop server replica_auth")
test_run:cmd("cleanup server replica_auth")
test_run:cmd("delete server replica_auth")
test_run:cleanup_cluster()

box.schema.user.drop('cluster')

--
-- Test case for gh-3610. Before the fix replica would fail with the assertion
-- when trying to connect to the same master twice.
--
box.schema.user.grant('guest', 'replication')
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")
replication = box.cfg.replication[1]
box.cfg{replication = {replication, replication}}

-- Check the case when duplicate connection is detected in the background.
test_run:cmd("switch default")
listen = box.cfg.listen
box.cfg{listen = ''}

test_run:cmd("switch replica")
box.cfg{replication_connect_quorum = 0, replication_connect_timeout = 0.01}
box.cfg{replication = {replication, replication}}

test_run:cmd("switch default")
box.cfg{listen = listen}
while test_run:grep_log('replica', 'duplicate connection') == nil do fiber.sleep(0.01) end

test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')

--
-- gh-3711 Do not restart replication on box.cfg in case the
-- configuration didn't change.
--
box.schema.user.grant('guest', 'replication')
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")

-- Access rights are checked only during reconnect. If the new
-- and old configurations are equivalent, no reconnect will be
-- issued and replication should continue working.
box.schema.user.revoke('guest', 'replication')
test_run:cmd("switch replica")
replication = box.cfg.replication[1]
box.cfg{replication = {replication}}
box.info.status == 'running'
box.cfg{replication = replication}
box.info.status == 'running'

-- Check that comparison of tables works as expected as well.
test_run:cmd("switch default")
box.schema.user.grant('guest', 'replication')
test_run:cmd("switch replica")
replication = box.cfg.replication
table.insert(replication, box.cfg.listen)
test_run:cmd("switch default")
box.schema.user.revoke('guest', 'replication')
test_run:cmd("switch replica")
box.cfg{replication = replication}
box.info.status == 'running'

test_run:cmd("switch default")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
test_run:cleanup_cluster()

--
-- gh-3704 move cluster id check to replica
--
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
box.schema.user.grant("guest", "replication")
test_run:cmd("start server replica")
test_run:grep_log("replica", "REPLICASET_UUID_MISMATCH")
box.info.replication[2].downstream.status
-- change master's cluster uuid and check that replica doesn't connect.
test_run:cmd("stop server replica")
_ = box.space._schema:replace{'cluster', tostring(uuid.new())}
-- master believes replica is in cluster, but their cluster UUIDs differ.
test_run:cmd("start server replica")
test_run:wait_log("replica", "REPLICASET_UUID_MISMATCH", nil, 1.0)
test_run:wait_downstream(2, {status = 'stopped'})

test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')

--
-- gh-4399 Check that an error reading WAL directory on subscribe
-- doesn't lead to a permanent replication failure.
--
box.schema.user.grant("guest", "replication")
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")

-- Make the WAL directory inaccessible.
fio = require('fio')
path = fio.abspath(box.cfg.wal_dir)
fio.chmod(path, 0)

-- Break replication on timeout.
replication_timeout = box.cfg.replication_timeout
box.cfg{replication_timeout = 9000}
test_run:cmd("switch replica")
test_run:wait_cond(function() return box.info.replication[1].upstream.status ~= 'follow' end)
require('fiber').sleep(box.cfg.replication_timeout)
test_run:cmd("switch default")
box.cfg{replication_timeout = replication_timeout}

-- Restore access to the WAL directory.
-- Wait for replication to be reestablished.
fio.chmod(path, tonumber('777', 8))
test_run:cmd("switch replica")
test_run:wait_cond(function() return box.info.replication[1].upstream.status == 'follow' end)
test_run:cmd("switch default")

test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')

--
-- gh-4424 Always enter orphan mode on error in replication
-- configuration change.
--
replication_connect_timeout = box.cfg.replication_connect_timeout
replication_connect_quorum = box.cfg.replication_connect_quorum
box.cfg{replication="12345", replication_connect_timeout=0.1, replication_connect_quorum=1}
box.info.status
box.info.ro
-- reset replication => leave orphan mode
box.cfg{replication=""}
box.info.status
box.info.ro
-- no switch to orphan when quorum == 0
box.cfg{replication="12345", replication_connect_quorum=0}
box.info.status
box.info.ro

-- we could connect to one out of two replicas. Set orphan.
box.cfg{replication_connect_quorum=2}
box.cfg{replication={box.cfg.listen, "12345"}}
box.info.status
box.info.ro
-- lower quorum => leave orphan mode
box.cfg{replication_connect_quorum=1}
box.info.status
box.info.ro

--
-- gh-3760: replication quorum 0 on reconfiguration should return
-- from box.cfg immediately.
--
replication = box.cfg.replication
box.cfg{                                                        \
    replication = {},                                           \
    replication_connect_quorum = 0,                             \
    replication_connect_timeout = 1000000                       \
}
-- The call below would hang, if quorum 0 is ignored, or checked
-- too late.
box.cfg{replication = {'localhost:12345'}}
box.info.status
box.cfg{                                                        \
    replication = {},                                           \
    replication_connect_quorum = replication_connect_quorum,    \
    replication_connect_timeout = replication_connect_timeout   \
}
