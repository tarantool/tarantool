#!/usr/bin/env tarantool

local test = require('tap').test('gh-5130')
local log = require('log')
test:plan(3)

--
-- gh-5130: panic on invalid "log"
--

-- Invalid log. No panic, just error
local _, err = pcall(log.cfg, {log=' :invalid'})
test:like(err, "expecting a file name or a prefix",
          'invalid log path reports an error')

-- Empty string - default log (to be compatible with box.cfg)
local ok = pcall(log.cfg, {log=''})
test:ok(ok, 'empty log path sets the default logger')

-- Dynamic reconfiguration - error, no info about invalid logger type
_, err = pcall(log.cfg, {log=' :invalid'})
test:like(err, "Can't set option 'log' dynamically",
          'invalid logger type is rejected')

os.exit(test:check() and 0 or 1)
