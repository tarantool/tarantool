env = require('test_run')
test_run = env.new()
test_run:cmd('switch default')
box.schema.user.grant('guest', 'replication')
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd('switch replica')
while box.space['_priv']:len() < 1 do fiber.sleep(0.001) end

r = box.info.replication
r.status == "follow"
r.lag < 1
r.idle < 1

box.space._schema:insert({'dup'})
test_run:cmd('switch default')
box.space._schema:insert({'dup'})
test_run:cmd('switch replica')
r = box.info.replication
r.status == "stopped"
r.message:match('Duplicate') ~= nil

box.cfg { replication_source = "" }
box.info.replication.status == "off"

-- Simulate a slow server to test replication info
control_ch = require('fiber').channel(1)
test_run:cmd("setopt delimiter ';'")
local digest = require('digest')
slowpoke_loop = function(s, peer)
    control_ch:get()
    local seed = digest.urandom(20)
    local handshake = string.format("Tarantool %-20s %-32s\n%-63s\n",
    "1.6.3-slowpoke", "@megastorage", digest.base64_encode(seed))
    s:write(handshake)
    s:readable()
    control_ch:get()
    s:shutdown()
    s:close()
end;
test_run:cmd("setopt delimiter ''");
slowpoke = require('socket').tcp_server('127.0.0.1', 0, slowpoke_loop)

uri = slowpoke:name()
box.cfg { replication_source = 'user:pass@'..uri.host..':'..uri.port }

r = box.info.replication
r.status == "connect"

control_ch:put(true)

require('fiber').sleep(0) -- wait replica to send auth request
r = box.info.replication
r.status == "auth"
r.lag < 1
r.idle < 1

--
-- gh-480: check replica reconnect on socket error
--
slowpoke:close()
control_ch:put("goodbye")
r = box.info.replication
r.status == "disconnected" and r.message:match("socket") ~= nil or r.status == 'auth'
r.idle < 1

slowpoke = require('socket').tcp_server(uri.host, uri.port, slowpoke_loop)
control_ch:put(true)

while box.info.replication.status == 'disconnected' do require('fiber').sleep(0) end
r = box.info.replication
r.status == 'connecting' or r.status == 'auth'
slowpoke:close()
control_ch:put("goodbye")


source = box.cfg.replication_source
box.cfg { replication_source = "" }
box.cfg { replication_source = source }
r = box.info.replication
r.idle < 1

test_run:cmd('switch default')
box.schema.user.revoke('guest', 'replication')
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
