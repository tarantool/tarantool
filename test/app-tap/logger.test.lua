#!/usr/bin/env tarantool

local io = require('io')
local log = require('log')
local errno = require('errno')
local fiber = require('fiber')

local test = require('tap').test('log')
test:plan(31)

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

-- checking log level set using require('cfg').level (since log.* functions are
-- proxy for base logger now)
do
    test:is(log.level(), 5, "default loglevel")

    -- testing in caps
    log.level('WARN')
    test:is(log.level(), 4, "warn loglevel is set")

    log.warn("warning")
    test:ok(file:read():find("W> warning"), "warning is printed")
    log.info("info")
    test:isnil(file:read(), "info isn't printed")

    -- testing string
    log.level('error')
    test:is(log.level(), 2, "error loglevel is set")

    log.warn("warning")
    test:isnil(file:read(), "warning isn't printed")
    log.error("error")
    test:ok(file:read():find("E> error"), "error is printed")

    log.level("info")
end

do -- check log object and basic level
    local log_object = log.new({ name = "log_object_name", level = "error" })

    log_object:info("do not print")
    test:isnil(file:read(), "info isn't printed")

    log_object:error("print it, please")
    test:ok(file:read():find("E> print it, please"), "error is printed")

    log.info("must be printed")
    test:ok(file:read():match("I> must be printed"), "info isn't printed")
end

do -- check log object and tracebacks
    local log_object = log.new({
        name = "log_object_name",
        backtrace_on_error = true
    })

    local function helper(level)
        log_object[level](log_object, ("some %s text"):format(level))
    end

    -- no stacktrace here
    helper("warn")
    test:ok(file:read():find("W> some warn text"), "warning is here")
    test:isnil(file:read(), "no stacktrace for warn")

    -- stacktrace here
    helper("error")
    test:ok(file:read():find("E> some error text"), "error is here")
    local cnt = 0
    repeat
        cnt = cnt + 1
        s = file:read()
        if cnt == 1 then
            test:ok(s:find("function 'helper'"), "first line is OK")
        elseif cnt == 2 then
            test:ok(s:find("logger.test.lua"), "second line is OK")
        elseif s ~= nil then
            test:fail()
        end
    until (s == nil)
    test:is(cnt - 1, 2, "right count of lines in backtrace")

    -- and here
    errno(22)
    helper("syserror")
    test:ok(file:read():match("!> some syserror text: Invalid argument"),
            'good syserror message on errno(22)')
    local cnt = 0
    repeat
        cnt = cnt + 1
        s = file:read()
        if cnt == 1 then
            test:ok(s:find("function 'helper'"), "first line is OK")
        elseif cnt == 2 then
            test:ok(s:find("logger.test.lua"), "second line is OK")
        elseif s ~= nil then
            test:fail()
        end
    until (s == nil)
    test:is(cnt - 1, 2, "right count of lines in backtrace")
end

os.exit(test:check() and 0 or 1)
