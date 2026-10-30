test_run = require('test_run').new()

--
-- gh-3883: Replication failure: relay may report that an xlog
-- is corrupted if it it currently being written to.
--
s = box.schema.space.create('test')
_ = s:create_index('primary')

-- Deploy a replica.
box.schema.user.grant('guest', 'replication')
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")

-- Setup replica => master channel.
box.cfg{replication = test_run:cmd("eval replica 'return box.cfg.listen'")}

-- Disable master => replica channel.
test_run:cmd("switch replica")
replication = box.cfg.replication
box.cfg{replication = {}}
test_run:cmd("switch default")

-- Write some xlogs on the master.
test_run:cmd("setopt delimiter ';'")
for i = 1, 100 do
    box.begin()
    for j = 1, 100 do
        s:replace{1, require('digest').urandom(1000)}
    end
    box.commit()
end;
test_run:cmd("setopt delimiter ''");

-- Enable master => replica channel and wait for the replica to catch up.
-- The relay handling replica => master channel on the replica will read
-- an xlog while the applier is writing to it. Although applier and relay
-- are running in different threads, there shouldn't be any rw errors.
test_run:cmd("switch replica")
box.cfg{replication = replication}
test_run:wait_cond(function()                    \
    local r = box.info.replication[1]            \
    return (r ~= nil and r.downstream ~= nil and \
            r.downstream.status ~= 'stopped')    \
end) or require('log').error(box.info)
test_run:cmd("switch default")

-- Cleanup.
box.cfg{replication = {}}
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')
s:drop()
