env = require('test_run')
test_run = env.new()
test_run:cmd("push filter '(error: .*)\\.lua:[0-9]+: ' to '\\1.lua:<line>: '")
box.cfg.nosuchoption = 1
cfg_filter(box.cfg)
-- must be read-only
box.cfg()
cfg_filter(box.cfg)

-- check that cfg with unexpected parameter fails.
box.cfg{sherlock = 'holmes'}

-- check that cfg with unexpected type of parameter failes
box.cfg{listen = {}}
box.cfg{wal_dir = 0}
box.cfg{coredump = 'true'}
