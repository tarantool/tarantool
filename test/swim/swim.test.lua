test_run = require('test_run').new()
test_run:cmd("push filter '\\.lua.*:[0-9]+: ' to '.lua:<line>: '")
--
-- gh-3234: SWIM gossip protocol.
--

-- Invalid cfg parameters.
swim.new(1)
swim.new({uri = true})
swim.new({heartbeat_rate = 'rate'})
swim.new({ack_timeout = 'timeout'})
swim.new({gc_mode = 'not a mode'})
swim.new({gc_mode = 0})
swim.new({uuid = 123})
swim.new({uuid = '1234'})

-- Valid parameters, but invalid configuration.
swim.new({})
swim.new({uuid = uuid(1)})
swim.new({uri = uri()})

-- Check manual deletion.
s = swim.new({uuid = uuid(1), uri = uri()})
s:delete()
s:cfg({})
s = nil
_ = collectgarbage('collect')

s = swim.new({uuid = uuid(1), uri = uri()})
s:quit()
s:is_configured()
s = nil
_ = collectgarbage('collect')

s = swim.new({uuid = uuid(1), uri = uri()})
s:is_configured()
s:size()
s.cfg
s.cfg.gc_mode = 'off'
s:cfg{gc_mode = 'off'}
s.cfg
s.cfg.gc_mode
s.cfg.uuid
s.cfg()
s:cfg({wrong_opt = 100})
s:delete()

-- Reconfigure.
s = swim.new()
s:is_configured()
-- Check that not configured instance does not provide most of
-- methods.
s.quit == nil
s:cfg({uuid = uuid(1), uri = uri()})
s.quit ~= nil
s:is_configured()
s:size()
s = nil
_ = collectgarbage('collect')

-- Invalid usage.
s = swim.new()
s.delete()
s.is_configured()
s.cfg()
s:delete()

--
-- Basic member table manipulations.
--
s1 = swim.new({uuid = uuid(1), uri = uri(), heartbeat_rate = 0.01})
s2 = swim.new({uuid = uuid(2), uri = listen_uri, heartbeat_rate = 0.01})

s1.broadcast()
s1:broadcast('wrong port')
-- Note, broadcast takes a port, not a URI.
s1:broadcast('127.0.0.1:3333')
-- Ok to broadcast on default port.
s1:broadcast()

s1:broadcast(listen_port)
while s2:size() ~= 2 do fiber.sleep(0.01) end
s1:size()
s2:size()

s2:delete()

s1.remove_member()
s1:remove_member(100)
s1:remove_member('1234')
s1:remove_member(uuid(2))
s1:size()

s1.add_member()
s1:add_member(100)
s1:add_member({uri = true})
s1:add_member({uri = listen_uri})
s1:add_member({uuid = uuid(2)})
s1:add_member({uri = listen_uri, uuid = uuid(2)})
s1:add_member({uri = listen_uri, uuid = uuid(2)})
s1:size()

s1:cfg({uuid = uuid(3)})
-- Can't remove self.
s1:remove_member(uuid(3))
-- Not existing.
s1:remove_member(uuid(4))
-- Old self.
s1:remove_member(uuid(1))

s1:delete()

s1 = swim.new({uuid = uuid(1), uri = uri()})
s2 = swim.new({uuid = uuid(2), uri = listen_uri})
s1.probe_member()
s1:probe_member()
s1:probe_member(true)
-- Not existing URI is ok - nothing happens.
s1:probe_member('127.0.0.1:1')
fiber.yield()
s1:size()
s1:probe_member(listen_uri)
while s1:size() ~= 2 do fiber.sleep(0.01) end
s2:size()

s1:delete()
s2:delete()

test_run:cmd("clear filter")
