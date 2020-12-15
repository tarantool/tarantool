#!/usr/bin/env tarantool

local test = require('tap').test('gh-5130')
local log = require('log')
test:plan(3)

--
-- gh-5130: panic on invalid "log"
--

-- Invalid log. No panic, just error
local _, err = pcall(log.cfg, {log=' :invalid'})
test:like(err, "expecting a file name or a prefix")

-- Empty string - default log (to be compatible with box.cfg)
local ok = pcall(log.cfg, {log=''})
test:ok(ok)

-- Dynamic reconfiguration - error, no info about invalid logger type
_, err = pcall(log.cfg, {log=' :invalid'})
test:like(err, "can't be set dynamically")

os.exit(test:check() and 0 or 1)
