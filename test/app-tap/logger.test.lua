#!/usr/bin/env tarantool

local io = require('io')
local log = require('log')
local errno = require('errno')
local fiber = require('fiber')

local test = require('tap').test('log')
test:plan(11)

--
-- Check that Tarantool creates ADMIN session for #! script
--
local filename = "1.log"
local message  = "Hello, World!"
box.cfg{
    log=filename,
    memtx_memory=107374182,
}
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
test:is(file:read():match('I>%s+(.*)'), "gh-700: %%s %%f %%d", "formatting")

log.info("gh-2340: %s %D")
test:is(file:read():match('I>%s+(.*)'), "gh-2340: %s %D", "formatting without arguments")

local function help() log.info("gh-2340: %s %s", 'help') end

xpcall(help, function(err)
    test:ok(err:match("bad argument #3"), "found error string")
    test:ok(err:match("logger.test.lua:36:"), "found error place")
end)

errno(0)
log.syserror("failed nothing")
test:ok(file:read():match('!> failed nothing: Undefined error: 0'),
        'good syserror message on errno(0)')

errno(22)
log.syserror("failed to open file '%s'", "filename")
test:ok(file:read():match("!> failed to open file 'filename': Invalid argument"),
        'good syserror message on errno(22)')

file:close()
log.rotate()

test:ok(log.pid() >= 0, "pid()")

-- logger uses 'debug', try to set it to nil
debug = nil
log.info("debug is nil")
debug = require('debug')

local file = io.open(filename)
while file:read() do
    fiber.yield()
end

local function another_helper() log.trace() end

another_helper()

local cnt = 0
repeat
    cnt = cnt + 1
    s = file:read()
    if cnt == 1 then
        test:ok(s:find("function 'another_helper'"), "first line is OK")
    elseif cnt == 2 then
        test:ok(s:find("logger.test.lua"), "second line is OK")
    elseif s ~= nil then
        test:fail()
    end
until s == nil

test:is(cnt - 1, 2, "right count of lines in backtrace")

os.exit(test:check() and 0 or 1)
