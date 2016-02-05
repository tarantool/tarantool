#!/usr/bin/env tarantool

local test = require('tap').test('log')
test:plan(3)

--
-- Check that Tarantool creates ADMIN session for #! script
-- 
local filename = "1.log"
local message = "Hello, World!"
box.cfg{
    logger=filename,
    slab_alloc_arena=0.1,
}
local log = require('log')
local io = require('io')
local file = io.open(filename)
while file:read() do
end
log.info(message)
local line = file:read()
test:is(line:sub(-message:len()), message, "message")

--
-- gh-700: Crash on calling log.info() with formatting characters
--
log.info("gh-700: %%s %%f %%d")
test:is(file:read():match('I>%s+(.*)'), "gh-700: %s %f %d", "formatting")

file:close()
log.rotate()

test:ok(log.logger_pid() >= 0, "logger_pid()")

-- logger uses 'debug', try to set it to nil
debug = nil
log.info("debug is nil")
debug = require('debug')

test:check()
os.exit()
