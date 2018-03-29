#!/usr/bin/env tarantool

local test = require('tap').test('log')
test:plan(24)

--
-- Check that Tarantool creates ADMIN session for #! script
--
local filename = "1.log"
local message = "Hello, World!"
box.cfg{
    log=filename,
    memtx_memory=107374182,
}
local log = require('log')
local fio = require('fio')
local json = require('json')
local fiber = require('fiber')
local file = io.open(filename)
while file:read() do
end
log.info(message)
local line = file:read()
test:is(line:sub(-message:len()), message, "message")
s, err = pcall(json.decode, line)
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

function help() log.info("gh-2340: %s %s", 'help') end

xpcall(help, function(err)
    test:ok(err:match("bad argument #3"), "found error string")
    test:ok(err:match("logger.test.lua:"), "found error place")
end)

file:close()

test:ok(log.pid() >= 0, "pid()")

-- logger uses 'debug', try to set it to nil
debug = nil
log.info("debug is nil")
debug = require('debug')

test:ok(log.info(true) == nil, 'check tarantool crash (gh-2516)')

s, err = pcall(box.cfg, {log_format='json', log="syslog:identity:tarantool"})
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
index = line:find('\n')
line = line:sub(1, index)
message = json.decode(line)
test:is(message.message, "log file has been reopened", "check message after log.rotate()")
file:close()
test:check()
os.exit()
