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
test_run:cmd('create server test with rpl_master=default, script="replication/replica_uuid.lua"')
test_run:cmd(string.format('start server test with args="%s"', replica_uuid))
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
box.cfg{read_only = true}
test_run:cmd(string.format('start server test with args="%s"', replica_uuid))
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
box.cfg{read_only = false}

-- gh-3160 - Send heartbeats if there are changes from a remote master only
SERVERS = { 'autobootstrap1', 'autobootstrap2', 'autobootstrap3' }

-- Deploy a cluster.
test_run:create_cluster(SERVERS, "replication", {args="0.1"})
test_run:wait_fullmesh(SERVERS)
test_run:cmd("switch autobootstrap1")
test_run = require('test_run').new()
box.cfg{replication_timeout = 0.01, replication_connect_timeout=0.01}
test_run:cmd("switch autobootstrap2")
test_run = require('test_run').new()
box.cfg{replication_timeout = 0.01, replication_connect_timeout=0.01}
test_run:cmd("switch autobootstrap3")
test_run = require('test_run').new()
fiber=require('fiber')
box.cfg{replication_timeout = 0.01, replication_connect_timeout=0.01}
_ = box.schema.space.create('test_timeout'):create_index('pk')
test_run:cmd("setopt delimiter ';'")
function test_timeout()
    for i = 0, 99 do 
        box.space.test_timeout:replace({1})
        fiber.sleep(0.005)
        local rinfo = box.info.replication
        if rinfo[1].upstream and rinfo[1].upstream.status ~= 'follow' or
           rinfo[2].upstream and rinfo[2].upstream.status ~= 'follow' or
           rinfo[3].upstream and rinfo[3].upstream.status ~= 'follow' then
            return error('Replication broken')
        end
    end
    return true
end ;
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
_ = test_run:wait_vclock("autobootstrap2", vclock)
test_run:cmd("switch autobootstrap2")
box.space.space1:select{}
test_run:cmd("switch autobootstrap1")
box.space.space1:drop()

test_run:cmd("switch default")
test_run:drop_cluster(SERVERS)

box.schema.user.revoke('guest', 'replication')

-- gh-3510 assertion failure in replica_on_applier_disconnect()
test_run:cmd('create server er_load1 with script="replication/er_load1.lua"')
test_run:cmd('create server er_load2 with script="replication/er_load2.lua"')
test_run:cmd('start server er_load1 with wait=False, wait_load=False')
-- instance er_load2 will fail with error ER_READONLY. this is ok.
-- We only test here that er_load1 doesn't assert.
test_run:cmd('start server er_load2 with wait=True, wait_load=True, crash_expected = True')
test_run:cmd('stop server er_load1')
-- er_load2 exits automatically.
test_run:cmd('cleanup server er_load1')
test_run:cmd('cleanup server er_load2')
