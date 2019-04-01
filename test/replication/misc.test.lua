uuid = require('uuid')
test_run = require('test_run').new()

box.schema.user.grant('guest', 'replication')

-- gh-2991 - Tarantool asserts on box.cfg.replication update if one of
-- servers is dead
replication_timeout = box.cfg.replication_timeout
replication_connect_timeout = box.cfg.replication_connect_timeout
box.cfg{replication_timeout=0.05, replication_connect_timeout=0.05, replication={}}
box.cfg{replication = {'127.0.0.1:12345', box.cfg.listen}}

-- gh-3606 - Tarantool crashes if box.cfg.replication is updated concurrently
fiber = require('fiber')
c = fiber.channel(2)
f = function() fiber.create(function() pcall(box.cfg, {replication = {12345}}) c:put(true) end) end
f()
f()
c:get()
c:get()

box.cfg{replication_timeout = replication_timeout, replication_connect_timeout = replication_connect_timeout}

-- gh-3111 - Allow to rebootstrap a replica from a read-only master
replica_uuid = uuid.new()
test_run:cmd('create server misc_gh3111 with rpl_master=default, script="replication/replica_uuid.lua"')
test_run:cmd(string.format('start server misc_gh3111 with args="%s"', replica_uuid))
test_run:cmd('stop server misc_gh3111')
test_run:cmd('cleanup server misc_gh3111')
box.cfg{read_only = true}
test_run:cmd(string.format('start server misc_gh3111 with args="%s"', replica_uuid))
test_run:cmd('stop server misc_gh3111')
test_run:cmd('cleanup server misc_gh3111')
box.cfg{read_only = false}
test_run:cmd('delete server misc_gh3111')
test_run:cleanup_cluster()

-- gh-3160 - Send heartbeats if there are changes from a remote master only
SERVERS = { 'misc1', 'misc2', 'misc3' }

-- Deploy a cluster.
test_run:create_cluster(SERVERS, "replication", {args="0.1"})
test_run:wait_fullmesh(SERVERS)
test_run:cmd("switch misc1")
test_run = require('test_run').new()
box.cfg{replication_timeout = 0.03, replication_connect_timeout=0.03}
test_run:cmd("switch misc2")
test_run = require('test_run').new()
box.cfg{replication_timeout = 0.03, replication_connect_timeout=0.03}
test_run:cmd("switch misc3")
test_run = require('test_run').new()
fiber=require('fiber')
box.cfg{replication_timeout = 0.03, replication_connect_timeout=0.03}
_ = box.schema.space.create('test_timeout'):create_index('pk')
test_run:cmd("setopt delimiter ';'")
function wait_follow(replicaA, replicaB)
    return test_run:wait_cond(function()
        return replicaA.status ~= 'follow' or replicaB.status ~= 'follow'
    end, 0.1)
end ;
function test_timeout()
    local replicaA = box.info.replication[1].upstream or box.info.replication[2].upstream
    local replicaB = box.info.replication[3].upstream or box.info.replication[2].upstream
    local follows = test_run:wait_cond(function()
        return replicaA.status == 'follow' or replicaB.status == 'follow'
    end, 1)
    if not follows then error('replicas not in follow status') end
    for i = 0, 99 do 
        box.space.test_timeout:replace({1})
        if wait_follow(replicaA, replicaB) then
            return error(box.info.replication)
        end
    end
    return true
end ;
test_run:cmd("setopt delimiter ''");
test_timeout()

-- gh-3247 - Sequence-generated value is not replicated in case
-- the request was sent via iproto.
test_run:cmd("switch misc1")
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
vclock = test_run:get_vclock("misc1")
_ = test_run:wait_vclock("misc2", vclock)
test_run:cmd("switch misc2")
box.space.space1:select{}
test_run:cmd("switch misc1")
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

test_run:cmd('create server misc_gh3642 with rpl_master=default, script="replication/replica.lua"')
test_run:cmd('start server misc_gh3642')
test_run:cmd('switch misc_gh3642')
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

test_run:cmd("stop server misc_gh3642")
test_run:cmd("cleanup server misc_gh3642")
test_run:cmd("delete server misc_gh3642")
test_run:cleanup_cluster()

box.schema.user.revoke('guest', 'replication')

-- gh-3510 assertion failure in replica_on_applier_disconnect()
test_run:cmd('create server misc_gh3510_1 with script="replication/er_load1.lua"')
test_run:cmd('create server misc_gh3510_2 with script="replication/er_load2.lua"')
test_run:cmd('start server misc_gh3510_1 with wait=False, wait_load=False')
-- instance misc_gh3510_2 will fail with error ER_READONLY. this is ok.
-- We only test here that misc_gh3510_1 doesn't assert.
test_run:cmd('start server misc_gh3510_2 with wait=True, wait_load=True, crash_expected = True')
test_run:cmd('stop server misc_gh3510_1')
-- misc_gh3510_2 exits automatically.
test_run:cmd('cleanup server misc_gh3510_1')
test_run:cmd('cleanup server misc_gh3510_2')
test_run:cmd('delete server misc_gh3510_1')
test_run:cmd('delete server misc_gh3510_2')
test_run:cleanup_cluster()

--
-- Test case for gh-3637. Before the fix replica would exit with
-- an error. Now check that we don't hang and successfully connect.
--
fiber = require('fiber')
test_run:cmd("create server misc_gh3637 with rpl_master=default, script='replication/replica_auth.lua'")
test_run:cmd("start server misc_gh3637 with wait=False, wait_load=False, args='cluster:pass 0.05'")
-- Wait a bit to make sure replica waits till user is created.
fiber.sleep(0.1)
box.schema.user.create('cluster', {password='pass'})
box.schema.user.grant('cluster', 'replication')

while box.info.replication[2] == nil do fiber.sleep(0.03) end
vclock = test_run:get_vclock('default')
_ = test_run:wait_vclock('misc_gh3637', vclock)

test_run:cmd("stop server misc_gh3637")
test_run:cmd("cleanup server misc_gh3637")
test_run:cmd("delete server misc_gh3637")
test_run:cleanup_cluster()

box.schema.user.drop('cluster')

--
-- Test case for gh-3610. Before the fix replica would fail with the assertion
-- when trying to connect to the same master twice.
--
box.schema.user.grant('guest', 'replication')
test_run:cmd("create server misc_gh3610 with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server misc_gh3610")
test_run:cmd("switch misc_gh3610")
replication = box.cfg.replication[1]
box.cfg{replication = {replication, replication}}

-- Check the case when duplicate connection is detected in the background.
test_run:cmd("switch default")
listen = box.cfg.listen
box.cfg{listen = ''}

test_run:cmd("switch misc_gh3610")
box.cfg{replication_connect_quorum = 0, replication_connect_timeout = 0.03}
box.cfg{replication = {replication, replication}}

test_run:cmd("switch default")
box.cfg{listen = listen}
while test_run:grep_log('misc_gh3610', 'duplicate connection') == nil do fiber.sleep(0.03) end

test_run:cmd("stop server misc_gh3610")
test_run:cmd("cleanup server misc_gh3610")
test_run:cmd("delete server misc_gh3610")
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')

--
-- gh-3711 Do not restart replication on box.cfg in case the
-- configuration didn't change.
--
box.schema.user.grant('guest', 'replication')
test_run:cmd("create server misc_gh3711 with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server misc_gh3711")

-- Access rights are checked only during reconnect. If the new
-- and old configurations are equivalent, no reconnect will be
-- issued and replication should continue working.
box.schema.user.revoke('guest', 'replication')
test_run:cmd("switch misc_gh3711")
replication = box.cfg.replication[1]
box.cfg{replication = {replication}}
box.info.status == 'running'
box.cfg{replication = replication}
box.info.status == 'running'

-- Check that comparison of tables works as expected as well.
test_run:cmd("switch default")
box.schema.user.grant('guest', 'replication')
test_run:cmd("switch misc_gh3711")
replication = box.cfg.replication
table.insert(replication, box.cfg.listen)
test_run:cmd("switch default")
box.schema.user.revoke('guest', 'replication')
test_run:cmd("switch misc_gh3711")
box.cfg{replication = replication}
box.info.status == 'running'

test_run:cmd("switch default")
test_run:cmd("stop server misc_gh3711")
test_run:cmd("cleanup server misc_gh3711")
test_run:cmd("delete server misc_gh3711")
test_run:cleanup_cluster()

--
-- gh-3704 move cluster id check to replica
--
test_run:cmd("create server misc_gh3704 with rpl_master=default, script='replication/replica.lua'")
box.schema.user.grant("guest", "replication")
test_run:cmd("start server misc_gh3704")
test_run:grep_log("misc_gh3704", "REPLICASET_UUID_MISMATCH")
box.info.replication[2].downstream.status
-- change master's cluster uuid and check that replica doesn't connect.
test_run:cmd("stop server misc_gh3704")
_ = box.space._schema:replace{'cluster', tostring(uuid.new())}
-- master believes replica is in cluster, but their cluster UUIDs differ.
test_run:cmd("start server misc_gh3704")
test_run:wait_log("misc_gh3704", "REPLICASET_UUID_MISMATCH", nil, 1.0)
test_run:wait_cond(function() return box.info.replication[2].downstream.status == 'stopped' end) or box.info.replication[2].downstream.status

test_run:cmd("stop server misc_gh3704")
test_run:cmd("cleanup server misc_gh3704")
test_run:cmd("delete server misc_gh3704")
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')
