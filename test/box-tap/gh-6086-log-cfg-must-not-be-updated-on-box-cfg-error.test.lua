#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('gh-6086-log-cfg-must-not-be-updated-on-box-cfg-error')

test:plan(1)

local log = require('log')
local log_cfg_copy = {}
for k, v in pairs(log.cfg) do log_cfg_copy[k] = v end
pcall(box.cfg, {log_nonblock = true})
test:is_deeply(log.cfg, log_cfg_copy, '\'log.cfg did not get updated on ' ..
               '\'box.cfg\' error')

os.exit(test:check() and 0 or 1)
