fiber = require('fiber')
digest = require('digest')
test_run = require('test_run').new()

--
-- gh-4042 applier read times out too fast when reading large rows.
--
box.schema.user.grant('guest', 'replication')
test_run:cmd('create server replica with rpl_master=default, script="replication/replica.lua"')
test_run:cmd('start server replica')
box.info.replication[2].downstream.status


-- make applier incapable of reading rows in one go, so that it
-- yields a couple of times.
test_run:cmd('switch replica')
box.error.injection.set("ERRINJ_SIO_READ_MAX", 1)
test_run:cmd('switch default')
s = box.schema.space.create('test')
_ = s:create_index('pk')
for i = 1,5 do box.space.test:replace{1, digest.urandom(1024)} collectgarbage('collect') end
-- replication_disconnect_timeout is 4 * replication_timeout, check that
-- replica doesn't time out too early.
test_run:cmd('setopt delimiter ";"')
ok = true;
start = fiber.time();
while fiber.time() - start < 3 * box.cfg.replication_timeout do
    if box.info.replication[2].downstream.status ~= 'follow' then
        ok = false
        break
    end
    fiber.sleep(0.001)
end;
test_run:cmd('setopt delimiter ""');

ok

s:drop()
test_run:cmd('stop server replica')
test_run:cmd('cleanup server replica')
test_run:cmd('delete server replica')
test_run:cleanup_cluster()
box.schema.user.revoke('guest', 'replication')

-- Rotate xlogs so as not to replicate the huge rows in
-- the following tests.
box.snapshot()
