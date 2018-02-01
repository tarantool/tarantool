fiber = require('fiber')

-- Test Lua from admin console. Whenever producing output,
-- make sure it's a valid YAML
box.info.unknown_variable
box.info[23]
box.info['unknown_variable']
string.match(box.info.version, '^[1-9]') ~= nil
string.match(box.info.pid, '^[1-9][0-9]*$') ~= nil
box.info.id > 0
box.info.uuid == box.space._cluster:get(box.info.id)[2]
box.info.lsn >= 0
box.info.signature >= 0
box.info.ro == false
box.info.replication[1].id
box.info.status
string.len(box.info.uptime) > 0
string.match(box.info.uptime, '^[1-9][0-9]*$') ~= nil
box.info.cluster.uuid == box.space._schema:get{'cluster'}[2]
t = {}
for k, _ in pairs(box.info()) do table.insert(t, k) end
table.sort(t)
t

-- Tarantool 1.6.x compat
box.info.server.id == box.info.id
box.info.server.uuid == box.info.uuid
box.info.server.lsn == box.info.lsn
box.info.ro == box.info.server.ro
box.info().server.id == box.info.id
box.info().server.uuid == box.info.uuid
box.info().server.lsn == box.info.lsn
box.info().ro == box.info.server.ro

--
-- box.ctl.wait_ro and box.ctl.wait_rw
--
box.ctl.wait_ro("abc") -- invalid argument
box.ctl.wait_rw("def") -- invalid argument
box.info.ro -- false
box.ctl.wait_rw() -- success
box.ctl.wait_ro(0.001) -- timeout
box.cfg{read_only = true}
box.ctl.wait_ro() -- success
box.ctl.wait_rw(0.001) -- timeout

status, err = nil
f = fiber.create(function() status, err = pcall(box.ctl.wait_rw) end)
fiber.sleep(0.001)
f:cancel()
while f:status() ~= 'dead' do fiber.sleep(0.001) end
status, err -- fiber is cancelled
box.cfg{read_only = false}
status, err = nil
f = fiber.create(function() status, err = pcall(box.ctl.wait_ro) end)
fiber.sleep(0.001)
f:cancel()
while f:status() ~= 'dead' do fiber.sleep(0.001) end
status, err -- fiber is cancelled

ch = fiber.channel(1)
_ = fiber.create(function() box.ctl.wait_ro() ch:put(box.info.ro) end)
fiber.sleep(0.001)
box.cfg{read_only = true}
ch:get() -- true
_ = fiber.create(function() box.ctl.wait_rw() ch:put(box.info.ro) end)
fiber.sleep(0.001)
box.cfg{read_only = false}
ch:get() -- false
