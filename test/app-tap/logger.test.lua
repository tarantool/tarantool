#!/usr/bin/env tarantool

local log = require('log')

local test = require('tap').test('log')
test:plan(142)

local function test_invalid_cfg(cfg_method, cfg, name, expected)
    local _, err = pcall(cfg_method, cfg)
    test:ok(tostring(err):find(expected) ~= nil, name)
end

local function test_allowed_types(cfg_method, cfg, name, allowed_types)
    local prefix
    if string.find(allowed_types, ',') then
        prefix = 'should be one of types '
    else
        prefix = 'should be of type '
    end
    test_invalid_cfg(cfg_method, cfg, name, prefix .. allowed_types)
end

-- Test allowed types for options
test_allowed_types(log.cfg, {log =  1},
                   'log.cfg allowed log types', 'string')
test_allowed_types(log.cfg, {level = true},
                   'log.cfg allowed level types', 'number, string')
test_allowed_types(log.cfg, {format = true},
                   'log.cfg allowed format types', 'string')
test_allowed_types(log.cfg, {nonblock = 'hi'},
                   'log.cfg allowed nonblock types', 'boolean')
test_allowed_types(box.cfg, {log = 1},
                   'box.cfg allowed log types', 'string')
test_allowed_types(box.cfg,{log_level = true},
                   'box.cfg allowed log_level types', 'number, string')
test_allowed_types(box.cfg, {log_format = true},
                   'box.cfg allowed log_format types', 'string')
test_allowed_types(box.cfg, {log_nonblock = 'hi'},
                   'box.cfg allowed log_nonblock types', 'boolean')

-- gh-7447
test_invalid_cfg(log.cfg, {log = 'syslog:xxx'},
                 "log.cfg invalid syslog",
                 "bad option 'xxx'")

test_invalid_cfg(log.cfg, {log = 'xxx:'},
                 "log.cfg invalid logger prefix",
                 "expecting a file name or a prefix, such as " ..
                    "'|', 'pipe:', 'syslog:'")

test_invalid_cfg(log.cfg, {format = 'xxx'},
                 "log.cfg invalid format",
                 "expected 'plain' or 'json'")

test_invalid_cfg(log.cfg, {nonblock = true},
                 "log.cfg nonblock and stderr",
                 'the option is incompatible with file/stderr logger')

test_invalid_cfg(log.cfg, {log = '1.log', nonblock = true},
                 "log.cfg nonblock and file",
                 'the option is incompatible with file/stderr logger')

test_invalid_cfg(log.cfg, {format = 'xxx'},
                 "log.cfg invalid format",
                 "expected 'plain' or 'json'")

test_invalid_cfg(log.cfg, {xxx = 1},
                 "log.cfg unexpected option",
                 'unexpected option')

--
-- gh-5121: Allow to use 'json' output before box.cfg()
--
local _, err = pcall(log.log_format, 'json')
test:ok(err == nil)

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

-- Try to change options that can't be changed on first box.cfg()
test_invalid_cfg(box.cfg, {log = '2.log'},
                 "reconfigure logger thru first box.cfg",
                 "Can't set option 'log' dynamically")
test_invalid_cfg(box.cfg, {log_nonblock = true},
                 "reconfigure nonblock thru first box.cfg",
                 "Can't set option 'log_nonblock' dynamically")

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
test:ok(log.cfg.level == 'fatal' and box.cfg.log_level == 'fatal',
        'both got fatal')
log.cfg({level='syserror'})
test:ok(log.cfg.level == 'syserror' and box.cfg.log_level == 'syserror',
        'both got syserror')
log.cfg({level='error'})
test:ok(log.cfg.level == 'error' and box.cfg.log_level == 'error',
        'both got error')
log.cfg({level='crit'})
test:ok(log.cfg.level == 'crit' and box.cfg.log_level == 'crit',
        'both got crit')
log.cfg({level='warn'})
test:ok(log.cfg.level == 'warn' and box.cfg.log_level == 'warn',
        'both got warn')
log.cfg({level='info'})
test:ok(log.cfg.level == 'info' and box.cfg.log_level == 'info',
        'both got info')
log.cfg({level='verbose'})
test:ok(log.cfg.level == 'verbose' and box.cfg.log_level == 'verbose',
        'both got verbose')
log.cfg({level='debug'})
test:ok(log.cfg.level == 'debug' and box.cfg.log_level == 'debug',
        'both got debug')

log.cfg{level = 4}
test:ok(log.cfg.level == 4, "log.cfg number level then read log.cfg")
test:ok(box.cfg.log_level == 4, "log.cfg number level then read box.cfg")

box.cfg{log_level = 5}
test:ok(log.cfg.level == 5, "box.cfg number level then read log.cfg")
test:ok(box.cfg.log_level == 5, "box.cfg number level then read box.cfg")

box.cfg{log_level = 'warn'}
test:ok(log.cfg.level == 'warn', "box.cfg string level then read log.cfg")
test:ok(box.cfg.log_level == 'warn', "box.cfg string level then read box.cfg")

box.cfg{
    log = filename,
    log_level = 6,
    memtx_memory = 107374182,
}

-- Try to change options that can't be changed on non-first box.cfg()
test_invalid_cfg(box.cfg, {log = '2.log'},
                 "reconfigure logger thru non-first box.cfg",
                 "Can't set option 'log' dynamically")
test:ok(box.cfg.log == filename, "filename match")
test:ok(box.cfg.log_level == 6, "loglevel match")
verify_keys("box.cfg static error")

test_invalid_cfg(box.cfg, {log_nonblock = true},
                 "reconfigure nonblock thru non-first box.cfg",
                 "Can't set option 'log_nonblock' dynamically")

-- Test invalid values for setters

_, err = pcall(log.log_format, {})
test:ok(tostring(err):find('should be of type string') ~= nil,
        "invalid format setter value type")

_, err = pcall(log.level, {})
test:ok(tostring(err):find('should be one of types number, string') ~= nil,
        "invalid format setter value type")

-- Change format and levels.

_, err = pcall(log.log_format, 'json')
test:ok(err == nil, "format setter result")
test:ok(log.cfg.format == 'json', "format setter cfg")
verify_keys("format setter verify keys")

_, err = pcall(log.level, 1)
test:ok(err == nil, "level setter number result")
test:ok(log.cfg.level == 1, "level setter number cfg")
verify_keys("level setter number verify keys")

_, err = pcall(log.level, 'warn')
test:ok(err == nil, "change log level")
test:ok(log.cfg.level == 'warn', "level setter string")
verify_keys("level setter string verify keys")

-- Check reset works thru log.cfg
log.cfg{level = box.NULL}
test:ok(log.cfg.level == 5, "reset of level thru log.cfg")

log.cfg{format = ""}
test:ok(log.cfg.format == 'plain', "reset of plain thru log.cfg")

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

log.info({"Hello", 123456, key1 = "val1", key2 = "val2", level = "bad"})
line = file:read():match('I>%s+(.*)')
test:is(line, '{"1":"Hello","2":123456,"key1":"val1","key2":"val2","level":"bad"}',
        "table is handled as json")
--
--gh-2923 dropping message field
--
log.info({message="value"})
test:is(file:read():match('I>%s+(.*)'), '{"message":"value"}', "message not dropped")
--
-- gh-3853 log.info spoils input (plain format)
--
local gh3853 = {file = 'c://autorun.bat'}
log.info(gh3853)
test:is(gh3853.file, "c://autorun.bat", "gh3853 is not modified")
file:read() -- skip line

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
-- gh-10918 json logger truncates large messages to 1024 bytes
log.info(string.rep('a', 32000))
line = file:read()
test:ok(line:len() < 16384, "big line truncated")
test:ok(line:len() > 16000, "big line is not too small")
message = json.decode(line)
test:istable(message, "json is valid for big message")
max_message_len = message.message:len()

log.info(string.format("%s\"", string.rep("b", max_message_len - 1)))
line = file:read()
test:ok(line:len() > 16000, "big line, ending with \", is not too small")
message = json.decode(line)
test:istable(message, "json is valid for big line, ending with \"")
test:is(message.message:endswith("b"), true, "big line, ends with \"a\"")

log.info(string.format("%s\"", string.rep("c", max_message_len - 2)))
line = file:read()
test:ok(line:len() > 16000, "big line, ending with \", is not too small")
message = json.decode(line)
test:istable(message, "json is valid for big line, ending with \"")
test:is(message.message:endswith("\""), true, "big line, ends with \"")

log.info("")
message = json.decode(file:read())
test:is(message.message, "", "empty message")

log.info()
message = json.decode(file:read())
test:isnil(message.message, "nil message")

log.info({message = string.rep("d", 32000)})
line = file:read()
test:ok(line:len() < 16384, "big line truncated")
test:ok(line:len() > 16000, "big line is not too small")
--[[ @TODO: This is broken, because the resulting JSON will not be valid
message = json.decode(line)
test:istable(message, "json is valid for big message")
]]--

n = 0
for message_len = max_message_len - 5, max_message_len + 5 do
    letter = string.char(string.byte("e") + n)
    n = n + 1
    expected_len = math.min(max_message_len, message_len)
    log.info(string.rep(letter, message_len))
    local line
    while not line do
        line = file:read()
    end
    message = json.decode(line)
    test:is(message.message:len(), expected_len, "message length is correct")
end

-- gh-3853 log.info spoils input (json format)
local gh3853 = {file = 'c://autorun.bat'}
log.info(gh3853)
test:is(gh3853.file, "c://autorun.bat", "gh3853 is not modified")
local line = file:read()

-- gh-7955 assertion in say_format_json
log.info({"Hello"})
message = json.decode(file:read())
test:is(message.message, "Hello", "table without keys")

log.info({nil, 2, 3})
message = json.decode(file:read())
test:is(message.message, nil, "nil first element")

log.info({})
message = json.decode(file:read())
test:is(message.message, "", "message is empty")

log.info({message = "My message"})
message = json.decode(file:read())
test:is(message.message, "My message", "message is correct")

log.info({"Hello", 123456, key1 = "val1", key2 = "val2", level = "bad"})
message = json.decode(file:read())
test:is(message.message, "Hello", "message is correct")
test:is(message['2'], 123456, "message['2'] is correct")
test:is(message.key1, "val1", "message.key1 is correct")
test:is(message.key2, "val2", "message.key2 is correct")
test:is(message.level, "INFO", "internal key is not affected")

local gh7955 = { param = 42 }
setmetatable(gh7955, {__serialize = function() return "gh7955 <param: 42>" end})
log.info(gh7955)
message = json.decode(file:read())
test:is(message.message, "gh7955 <param: 42>", "__serialize returns string")
test:is(message.param, nil, "__serialize overrides gh7955 fields")

setmetatable(gh7955, {__serialize = function() return { 111, 112 } end})
log.info(gh7955)
message = json.decode(file:read())
test:is(message.message, 111, "__serialize returns array")
test:is(message['2'], 112, "message['2'] is correct")
test:is(message.param, nil, "__serialize overrides gh7955 fields")

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
