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

-- Check that replication doesn't freeze if slave bumps LSN
-- while master is down (gh-3038). To do this,
-- 1. Stop replication by injecting an error on the slave.
-- 2. Bump LSN on the slave while replication is inactive.
-- 3. Restore replication.
-- 4. Generate some records on the master.
-- 5. Make sure they'll make it to the slave.
test_run:cmd("switch replica")
box.error.injection.set("ERRINJ_WAL_WRITE", true)
test_run:cmd("switch default")
s:replace{9000, "won't make it"}
test_run:cmd("switch replica")
while box.info.replication[1].upstream.status == 'follow' do fiber.sleep(0.0001) end
box.error.injection.set("ERRINJ_WAL_WRITE", false)
s:replace{9001, "bump lsn"}
box.cfg{replication={}}
box.cfg{replication = os.getenv('MASTER')}
test_run:cmd("switch default")
test_f(61, true)
test_run:cmd("switch replica")
wait_repl(70)
test_run:cmd("switch default")

test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")

-- Set minuscule timeout to make replication stop
-- immediately after join.
box.cfg{replication_timeout = 0.0001}

test_run:cmd("start server replica")
test_run:cmd("switch replica")
fiber = require'fiber'
while box.info.replication[1].upstream.message ~= 'timed out' do fiber.sleep(0.0001) end

test_run:cmd("switch default")
-- Disable heartbeat messages on the master so as not
-- to trigger acks on the replica.
errinj.set("ERRINJ_RELAY_REPORT_INTERVAL", 5)
box.cfg{replication_timeout = 0.05}
test_run:cmd("switch replica")
-- wait for reconnect
while box.info.replication[1].upstream.status ~= 'follow' do fiber.sleep(0.0001) end
box.info.replication[1].upstream.status
box.info.replication[1].upstream.lag > 0
box.info.replication[1].upstream.lag < 1
-- wait for ack timeout
while box.info.replication[1].upstream.message ~= 'timed out' do fiber.sleep(0.0001) end

test_run:cmd("switch default")
errinj.set("ERRINJ_RELAY_REPORT_INTERVAL", 0)
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
errinj.set("ERRINJ_RELAY_EXIT_DELAY", 0)

box.cfg{replication_timeout = 0.01}

test_run:cmd("create server replica_timeout with rpl_master=default, script='replication/replica_timeout.lua'")
test_run:cmd("start server replica_timeout with args='0.01'")
test_run:cmd("switch replica_timeout")

fiber = require('fiber')
while box.info.replication[1].upstream.status ~= 'follow' do fiber.sleep(0.0001) end
box.info.replication[1].upstream.status

test_run:cmd("switch default")
errinj.set("ERRINJ_RELAY_REPORT_INTERVAL", 5)

test_run:cmd("switch replica_timeout")
-- Check replica's disconnection on timeout (gh-3025).
-- If master stops send heartbeat messages to replica,
-- due to infinite read timeout connection never breaks,
-- replica shows state 'follow' so old behaviour hangs
-- here in infinite loop.
while box.info.replication[1].upstream.message ~= 'timed out' do fiber.sleep(0.0001) end

test_run:cmd("switch default")
test_run:cmd("stop server replica_timeout")
test_run:cmd("cleanup server replica_timeout")
errinj.set("ERRINJ_RELAY_REPORT_INTERVAL", 0)

-- Check replica's ACKs don't prevent the master from sending
-- heartbeat messages (gh-3160).

test_run:cmd("start server replica_timeout with args='0.009'")
test_run:cmd("switch replica_timeout")

fiber = require('fiber')
while box.info.replication[1].upstream.status ~= 'follow' do fiber.sleep(0.0001) end
box.info.replication[1].upstream.status -- follow
for i = 0, 15 do fiber.sleep(0.01) if box.info.replication[1].upstream.status ~= 'follow' then break end end
box.info.replication[1].upstream.status -- follow

test_run:cmd("switch default")
test_run:cmd("stop server replica_timeout")
test_run:cmd("cleanup server replica_timeout")

box.snapshot()
for i = 0, 9999 do box.space.test:replace({i, 4, 5, 'test'}) end

-- Check that replication_timeout is not taken into account
-- during the join stage, i.e. a replica with a minuscule
-- timeout successfully bootstraps and breaks connection only
-- after subscribe.
test_run:cmd("start server replica_timeout with args='0.00001'")
test_run:cmd("switch replica_timeout")
fiber = require('fiber')
while box.info.replication[1].upstream.message ~= 'timed out' do fiber.sleep(0.0001) end

test_run:cmd("stop server default")
test_run:cmd("deploy server default")
test_run:cmd("start server default")
test_run:cmd("switch default")
test_run:cmd("stop server replica_timeout")
test_run:cmd("cleanup server replica_timeout")
