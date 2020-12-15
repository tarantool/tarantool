#!/usr/bin/env tarantool

local test = require('tap').test('log')
test:plan(64)

--
-- gh-5121: Allow to use 'json' output before box.cfg()
--
local log = require('log')
local _, err = pcall(log.log_format, 'json')
test:ok(err == nil)

-- We're not allowed to use json with syslog though.
_, err = pcall(log.cfg, {log='syslog:', format='json'})
test:ok(tostring(err):find("can\'t be used with syslog logger") ~= nil)

_, err = pcall(box.cfg, {log='syslog:', log_format='json'})
test:ok(tostring(err):find("can\'t be used with syslog logger") ~= nil)

-- switch back to plain to next tests
log.log_format('plain')

--
-- gh-689: various settings change from box.cfg/log.cfg interfaces
--
--
local box2log_keys = {
    ['log']             = 'log',
    ['log_nonblock']    = 'nonblock',
    ['log_level']       = 'level',
    ['log_format']      = 'format',
}

local function verify_keys(prefix)
    for k, v in pairs(box2log_keys) do
        local m = "%s: %s/%s (%s %s) are equal"
        test:ok(box.cfg[k] == log.cfg[v],
                m:format(prefix, k, v,
                         box.cfg[k], log.cfg[v]))
    end
end

-- Make sure the configuration defaults are fetched
-- correctly from log module
test:ok(log.cfg.log == nil, "log.cfg.log is nil")
test:ok(log.cfg.format == 'plain', "log.cfg.format is 'plain'")
test:ok(log.cfg.level == 5, "log.cfg.level is 5")
test:ok(log.cfg.nonblock == nil , "log.cfg.nonblock is nil")

-- Configure logging from log module
local filename = "1.log"

_, err = pcall(log.cfg, {log=filename, format='plain', level=6})
test:ok(err == nil, "valid log.cfg call")
test:ok(log.cfg.log == filename, "log.cfg.log ok")
test:ok(log.cfg.format == 'plain', "log.cfg.format is ok")
test:ok(log.cfg.level == 6, "log.cfg.level is 6")

-- switch to json mode
_, err = pcall(log.cfg, {format='json', level='verbose'})
test:ok(err == nil, "switch to json")

local message = "json message"
local json = require('json')
local file = io.open(filename)
while file:read() do
end

log.verbose(message)
local line = file:read()
local s = json.decode(line)
test:ok(s['message'] == message, "message match")

-- Now switch to box.cfg interface
box.cfg{
    log = filename,
    log_level = 6,
    memtx_memory = 107374182,
}
test:ok(box.cfg.log == filename, "filename match")
test:ok(box.cfg.log_level == 6, "loglevel match")
verify_keys("box.cfg")

-- Test symbolic names for loglevels
log.cfg({level='fatal'})
test:ok(log.cfg.level == 0 and box.cfg.log_level == 0, 'both got fatal')
log.cfg({level='syserror'})
test:ok(log.cfg.level == 1 and box.cfg.log_level == 1, 'both got syserror')
log.cfg({level='error'})
test:ok(log.cfg.level == 2 and box.cfg.log_level == 2, 'both got error')
log.cfg({level='crit'})
test:ok(log.cfg.level == 3 and box.cfg.log_level == 3, 'both got crit')
log.cfg({level='warn'})
test:ok(log.cfg.level == 4 and box.cfg.log_level == 4, 'both got warn')
log.cfg({level='info'})
test:ok(log.cfg.level == 5 and box.cfg.log_level == 5, 'both got info')
log.cfg({level='verbose'})
test:ok(log.cfg.level == 6 and box.cfg.log_level == 6, 'both got verbose')
log.cfg({level='debug'})
test:ok(log.cfg.level == 7 and box.cfg.log_level == 7, 'both got debug')

box.cfg{
    log = filename,
    log_level = 6,
    memtx_memory = 107374182,
}

-- Now try to change a static field.
_, err = pcall(box.cfg, {log_level = 5, log = "2.txt"})
test:ok(tostring(err):find("can\'t be set dynamically") ~= nil)
test:ok(box.cfg.log == filename, "filename match")
test:ok(box.cfg.log_level == 6, "loglevel match")
verify_keys("box.cfg static error")

-- Change format and levels.
_, err = pcall(log.log_format, 'json')
test:ok(err == nil, "change to json")
_, err = pcall(log.level, 1)
test:ok(err == nil, "change log level")
verify_keys("log change json and level")

-- Restore defaults
log.log_format('plain')
log.level(5)

--
-- Check that Tarantool creates ADMIN session for #! script
--
local filename = "1.log"
local message = "Hello, World!"
box.cfg{
    log=filename,
    memtx_memory=107374182,
}
local fio = require('fio')
local json = require('json')
local fiber = require('fiber')
local file = io.open(filename)
while file:read() do
end
log.info(message)
local line = file:read()
test:is(line:sub(-message:len()), message, "message")
local s = pcall(json.decode, line)
test:ok(not s, "plain")
--
-- gh-700: Crash on calling log.info() with formatting characters
--
log.info("gh-700: %%s %%f %%d")
test:is(file:read():match('I>%s+(.*)'), "gh-700: %%s %%f %%d", "formatting")

log.info("gh-2340: %s %D")
test:is(file:read():match('I>%s+(.*)'), "gh-2340: %s %D", "formatting without arguments")

log.info({key="value"})
test:is(file:read():match('I>%s+(.*)'), '{"key":"value"}', "table is handled as json")
--
--gh-2923 dropping message field
--
log.info({message="value"})
test:is(file:read():match('I>%s+(.*)'), '{"message":"value"}', "table is handled as json")

local function help() log.info("gh-2340: %s %s", 'help') end

xpcall(help, function(err)
    test:ok(err:match("bad argument #3"), "found error string")
    test:ok(err:match("logger.test.lua:"), "found error place")
end)

file:close()

test:ok(log.pid() >= 0, "pid()")

-- luacheck: ignore (logger uses 'debug', try to set it to nil)
debug = nil
log.info("debug is nil")
debug = require('debug')

test:ok(log.info(true) == nil, 'check tarantool crash (gh-2516)')

s = pcall(box.cfg, {log_format='json', log="syslog:identity:tarantool"})
test:ok(not s, "check json not in syslog")

box.cfg{log=filename,
    memtx_memory=107374182,
    log_format = "json"}

local file = io.open(filename)
while file:read() do
end

log.error("error")

local line = file:read()
message = json.decode(line)
test:is(type(message), 'table', "json valid in log.error")
test:is(message.level, "ERROR", "check type error")
test:is(message.message, "error", "check error message")

log.info({key="value", level=48})
local line = file:read()
message = json.decode(line)
test:is(type(message), 'table', "json valid in log.info")
test:is(message.level, "INFO", "check type info")
test:is(message.message, nil, "check message is nil")
test:is(message.key, "value", "custom table encoded")

log.info('this is "')
local line = file:read()
message = json.decode(line)
test:is(message.message, "this is \"", "check message with escaped character")

-- gh-3248 trash in log file with logging large objects
log.info(string.rep('a', 32000))
line = file:read()
test:ok(line:len() < 20000, "big line truncated")

log.info("json")
local line = file:read()
message = json.decode(line)
test:is(message.message, "json", "check message with internal key word")
log.log_format("plain")
log.info("hello")
line = file:read()
test:ok(not line:match("{"), "log change format")
s, e = pcall(log.log_format, "non_format")
test:ok(not s, "bad format")
file:close()

log.log_format("json")

fio.rename(filename, filename .. "2")
log.rotate()
file = fio.open(filename)
while file == nil do file = fio.open(filename) fiber.sleep(0.0001) end
line = file:read()
while line == nil or line == ""  do line = file:read() fiber.sleep(0.0001) end
local index = line:find('\n')
line = line:sub(1, index)
message = json.decode(line)
test:is(message.message, "log file has been reopened", "check message after log.rotate()")
file:close()
test:check()
os.exit()
