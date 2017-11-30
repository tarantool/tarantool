env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
box.schema.user.grant('guest', 'read,write,execute', 'universe')

errinj = box.error.injection

box.schema.user.grant('guest', 'replication')
s = box.schema.space.create('test', {engine = engine});
index = s:create_index('primary')

test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd("switch replica")

fiber = require('fiber')

s = box.space.test
test_run:cmd("setopt delimiter ';'")
-- vinyl does not support index.len() so we use index.count() instead
function wait_repl(cnt)
    for i = 1, 20 do
        if s.index[0]:count() >= cnt then
            return true
        end
        fiber.sleep(0.01)
    end
    return false
end;
test_run:cmd("setopt delimiter ''");

test_run:cmd("switch default")
d = require('digest')

test_run:cmd("setopt delimiter ';'")
function test_f(st, tx)
    if tx then
        box.begin()
    end
    for i = st, st + 9 do
        local _ = s:insert({i, d.urandom(8192)})
    end
    if tx then
        box.commit()
    end
end;
test_run:cmd("setopt delimiter ''");

test_f(1)

errinj.set("ERRINJ_WAL_WRITE_PARTIAL", 16384)
test_f(11, true)

errinj.set("ERRINJ_WAL_WRITE_PARTIAL", -1)

test_f(11, true)
test_f(21, true)

test_run:cmd("switch replica")
wait_repl(30)

test_run:cmd("switch default")
box.space.test.index[0]:count()

errinj.set("ERRINJ_WAL_WRITE_DISK", true)
test_f(31, true)

errinj.set("ERRINJ_WAL_WRITE_DISK", false)

test_f(31, true)
test_f(41, true)

test_run:cmd("switch replica")
wait_repl(50)

test_run:cmd("switch default")
box.space.test.index[0]:count()

-- Check that master doesn't stall on WALs without EOF (gh-2294).
errinj.set("ERRINJ_WAL_WRITE_EOF", true)
box.snapshot()
test_f(51, true)
test_run:cmd("switch replica")
wait_repl(60)
test_run:cmd("switch default")
errinj.set("ERRINJ_WAL_WRITE_EOF", false)
box.snapshot()

test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")

box.cfg{replication_timeout = 0.01}

test_run:cmd("start server replica")
test_run:cmd("switch replica")
fiber = require'fiber'
while box.info.replication[1].upstream.message ~= 'timed out' do fiber.sleep(0.0001) end

test_run:cmd("switch default")
box.cfg{replication_timeout = 0.05}
test_run:cmd("switch replica")
-- wait for reconnect
while box.info.replication[1].upstream.status ~= 'follow' do fiber.sleep(0.0001) end
box.info.replication[1].upstream.status
box.info.replication[1].upstream.lag
-- wait for ack timeout
while box.info.replication[1].upstream.message ~= 'timed out' do fiber.sleep(0.0001) end

test_run:cmd("switch default")
box.cfg{replication_timeout = 5}

test_run:cmd("switch replica")
-- wait for reconnect
while box.info.replication[1].upstream.status ~= 'follow' do fiber.sleep(0.0001) end
-- wait for ack timeout again, should be ok
fiber.sleep(0.01)
{box.info.replication[1].upstream.status, box.info.replication[1].upstream.message}

test_run:cmd("switch default")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")

errinj = box.error.injection
errinj.set("ERRINJ_RELAY_EXIT_DELAY", 0.01)

test_run:cmd("start server replica")
test_run:cmd("switch replica")
fiber = require('fiber')
old_repl = box.cfg.replication
-- shutdown applier
box.cfg{replication = {}, replication_timeout = 0.1}
while box.info.replication[1].upstream ~= nil do fiber.sleep(0.0001) end
-- reconnect
box.cfg{replication = {old_repl}}
while box.info.replication[1].upstream.status ~= 'disconnected' do fiber.sleep(0.0001) end
while box.info.replication[1].upstream.status ~= 'follow' do fiber.sleep(0.0001) end

test_run:cmd("switch default")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")

box.snapshot()
for i = 0, 9999 do box.space.test:replace({i, 4, 5, 'test'}) end

test_run:cmd("create server replica_ack with rpl_master=default, script='replication/replica_ack.lua'")
test_run:cmd("start server replica_ack")
test_run:cmd("switch replica_ack")
box.info.replication[1].upstream.status

test_run:cmd("stop server default")
test_run:cmd("deploy server default")
test_run:cmd("start server default")
test_run:cmd("switch default")
test_run:cmd("stop server replica_ack")
test_run:cmd("cleanup server replica_ack")
