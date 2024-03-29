fiber = require 'fiber'
test_run = require('test_run').new()
net = require('net.box')

--
-- On_connect/disconnect triggers.
--
test_run:cmd('create server connecter with script = "box/proxy.lua"')
test_run:cmd('start server connecter')
test_run:cmd("set variable connect_to to 'connecter.listen'")
conn = net.connect(connect_to, { reconnect_after = 0.1 })
conn.state
connected_cnt = 0
disconnected_cnt = 0
function on_connect() connected_cnt = connected_cnt + 1 end
function on_disconnect() disconnected_cnt = disconnected_cnt + 1 end
_ = conn:on_connect(on_connect)
_ = conn:on_disconnect(on_disconnect)
test_run:cmd('stop server connecter')
test_run:cmd('start server connecter')
while conn.state ~= 'active' do fiber.sleep(0.1) end
connected_cnt
old_disconnected_cnt = disconnected_cnt
disconnected_cnt >= 1
conn:close()
disconnected_cnt == old_disconnected_cnt + 1
test_run:cmd('stop server connecter')
