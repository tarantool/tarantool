test_run = require('test_run').new()
test_run:cmd("restart server default")

box.schema.user.grant('guest', 'replication')

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

test_run:wait_upstream(1, {status = 'follow'})

test_run:cmd('switch default')

lim.rlim_cur = old_fno
rlimit.setrlimit(rlimit.RLIMIT_NOFILE, lim)

test_run:cmd("stop server sock")
test_run:cmd("cleanup server sock")
test_run:cmd("delete server sock")
test_run:cleanup_cluster()

box.schema.user.revoke('guest', 'replication')
