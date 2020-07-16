#!/usr/bin/env tarantool

-- Testing configuration updates

local tap = require('tap')
local test = tap.test('cfg')
test:plan(3)

local config = {
    pid_file = '1.pid',
    log="tarantool.log"
}

local status = pcall(box.cfg, config)
test:ok(status, 'initial config')

-- Assigning the same value to immutable key which is effectively a NOP,
-- expecting success
status = pcall(box.cfg, {pid_file = config.pid_file})
test:ok(status, 'assign the same value to immutable key (pid_file)')

-- Now change it to a different value, must fail
local result
status, result = pcall(box.cfg, {pid_file = 'Z'..config.pid_file})
test:ok(not status and
        result:match("Can't set option 'pid_file' dynamically"),
        'attempt to change immutable key (pid_file)')

test:check()
os.exit(0)

