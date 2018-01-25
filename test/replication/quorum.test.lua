test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

SERVERS = {'autobootstrap_guest1', 'autobootstrap_guest2', 'autobootstrap_guest3'}

-- Deploy a cluster.
test_run:create_cluster(SERVERS)
test_run:wait_fullmesh(SERVERS)

-- Create a new replica and switch to it.
test_run:cmd('create server test with script "replication/quorum.lua"')
test_run:cmd('start server test')
test_run:cmd('switch test')

-- Stop one master and try to restart the replica.
-- It should successfully restart because it has
-- replication_connect_quorum set to 2 (see quorum.lua)
-- and two other masters are still running.
test_run:cmd('stop server autobootstrap_guest1')
test_run:cmd('restart server test')

fio = require('fio')
fiber = require('fiber')

SERVERS = {'autobootstrap_guest1', 'autobootstrap_guest2', 'autobootstrap_guest3'}
SOCKET_DIR = fio.cwd()
test_run:cmd("setopt delimiter ';'")
function instance_uri(name)
    return SOCKET_DIR .. '/' .. name .. '.sock'
end;
function cfg_replication(servers)
    local replication = {}
    for _, srv in ipairs(servers) do
        table.insert(replication, instance_uri(srv))
    end
    box.cfg{replication = replication}
end;
test_run:cmd("setopt delimiter ''");

-- Set a stricter value for replication_connect_quorum and
-- check that replication configuration fails.
box.cfg{replication_connect_quorum = 3}
cfg_replication(SERVERS) -- fail
box.cfg{replication_connect_quorum = nil} -- default: wait for all
cfg_replication(SERVERS) -- fail

-- Lower replication quorum and check that replication
-- configuration succeeds.
box.cfg{replication_connect_quorum = 2}
cfg_replication(SERVERS) -- success

-- Start the master that was down and check that
-- the replica follows it. To do that, we need to
-- stop other masters.
test_run:cmd('start server autobootstrap_guest1')
test_run:cmd('stop server autobootstrap_guest2')
test_run:cmd('stop server autobootstrap_guest3')
test_run:cmd('switch autobootstrap_guest1')
box.space.test:auto_increment{'test'}
test_run:cmd('switch test')
while box.space.test:count() < 1 do fiber.sleep(0.001) end
box.space.test:select()

-- Cleanup.
test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
test_run:cmd('stop server autobootstrap_guest1')
for _, srv in ipairs(SERVERS) do test_run:cmd(string.format('cleanup server %s', srv)) end
