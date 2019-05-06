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

test_run:cmd("clear filter")
