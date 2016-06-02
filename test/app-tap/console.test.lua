#!/usr/bin/env tarantool

local tap = require('tap')
local console = require('console')
local socket = require('socket')
local yaml = require('yaml')
local fiber = require('fiber')
local ffi = require('ffi')
local log = require('log')

-- Supress console log messages
log.level(4)

local CONSOLE_SOCKET = '/tmp/tarantool-test-console.sock'
local IPROTO_SOCKET = '/tmp/tarantool-test-iproto.sock'
os.remove(CONSOLE_SOCKET)
os.remove(IPROTO_SOCKET)

--
local EOL = "\n...\n"

test = tap.test("console")

test:plan(34)

-- Start console and connect to it
local server = console.listen(CONSOLE_SOCKET)
test:ok(server ~= nil, "console.listen started")
local client = socket.tcp_connect("unix/", CONSOLE_SOCKET)
local handshake = client:read{chunk = 128}
test:ok(string.match(handshake, '^Tarantool .*console') ~= nil, 'Handshake')
test:ok(client ~= nil, "connect to console")

-- Execute some command
client:write("1\n")
test:is(yaml.decode(client:read(EOL))[1], 1, "eval")

-- doesn't crash and doesn't hang
client:write("_G\n")
test:is(#client:read(EOL) > 0, true, "_G")

-- Check internal state of `console` module
client:write("require('fiber').id()\n")
local fid1 = yaml.decode(client:read(EOL))[1]
local state = fiber.find(fid1).storage.console
local server_info = state.client:peer()
local client_info = state.client:name()
test:is(client_info.host, client_info.host, "state.socker:peer().host")
test:is(client_info.port, client_info.port, "state.socker:peer().port")
server_info = nil
client_info = nil

-- Check console.delimiter()
client:write("require('console').delimiter(';')\n")
test:is(yaml.decode(client:read(EOL)), '', "set delimiter to ';'")
test:is(state.delimiter, ';', "state.delimiter is ';'")
client:write("require('console').delimiter();\n")
test:is(yaml.decode(client:read(EOL))[1], ';', "get delimiter is ';'")
client:write("require('console').delimiter('');\n")
test:is(yaml.decode(client:read(EOL)), '', "clear delimiter")

box.cfg{
    listen=IPROTO_SOCKET;
    slab_alloc_arena=0.1,
    logger="tarantool.log",
}

-- Connect to iproto console (CALL)
client:write(string.format("require('console').connect('/')\n"))
-- error: Connection is not established
test:ok(yaml.decode(client:read(EOL))[1].error:find('not established'),
    'remote network error')

client:write(string.format("require('console').connect('%s')\n",
    IPROTO_SOCKET))
-- error: Execute access is denied for user 'guest' to function 'dostring
test:ok(yaml.decode(client:read(EOL))[1].error:find('denied'),
    'remote access denied')

-- create user
box.schema.user.create('test', { password = 'pass' })
client:write(string.format("require('console').connect('test:pass@%s')\n",
    IPROTO_SOCKET))
-- error: Execute access denied for user 'test' to function 'dostring
test:ok(yaml.decode(client:read(EOL))[1].error:find('denied'),
    'remote access denied')

-- Add permissions to execute for `test`
box.schema.user.grant('test', 'execute', 'universe')

client:write(string.format("require('console').connect('test:pass@%s')\n",
    IPROTO_SOCKET))
test:ok(yaml.decode(client:read(EOL)), "remote connect")

-- Log in with an empty password
box.schema.user.create('test2', { password = '' })
box.schema.user.grant('test2', 'execute', 'universe')

client:write(string.format("require('console').connect('test2@%s')\n",
    IPROTO_SOCKET))
test:ok(yaml.decode(client:read(EOL)), "remote connect")

client:write(string.format("require('console').connect('test2:@%s')\n",
    IPROTO_SOCKET))
test:ok(yaml.decode(client:read(EOL)), "remote connect")

-- Execute some command
client:write("require('fiber').id()\n")
local fid2 = yaml.decode(client:read(EOL))[1]
test:isnt(fid1, fid2, "remote eval")

test:is(state.remote.host, "unix/", "remote state.remote.host")
test:is(state.remote.port, IPROTO_SOCKET, "remote state.remote.port")
test:is(state.prompt, string.format("%s:%s", "unix/", IPROTO_SOCKET),
        "remote state.prompt")

-- Check exception handling (gh-643)
client:write("error('test')\n")
test:ok(yaml.decode(client:read(EOL))[1].error:match('test') ~= nil,
    "exception handling")
client:write("setmetatable({}, { __serialize = function() error('test') end})\n")
test:ok(yaml.decode(client:read(EOL))[1].error:match('test') ~= nil,
    "exception handling")

-- Disconnect from iproto
client:write("~.\n")
-- Check that iproto console has been disconnected
client:write("require('fiber').id()\n")
local fid1x = yaml.decode(client:read(EOL))[1]
test:is(fid1, fid1x, "remote disconnect")

-- Connect to admin port
client:write(string.format("require('console').connect('%s')\n",
    CONSOLE_SOCKET))
test:ok(yaml.decode(client:read(EOL))[1], 'admin connect')
client:write("2 + 2\n")
test:ok(yaml.decode(client:read(EOL))[1] == 4, "admin eval")

-- gh-1177: Error message for display of a net.box result
client:write("require('net.box').new('unix/', '"..IPROTO_SOCKET.."')\n")
test:isnil(yaml.decode(client:read(EOL))[1].error, "gh-1177 __serialize")
-- there is no way to disconnect here

-- Disconect from console
client:shutdown()
client:write('')
client:close()

-- Stop console
server:shutdown()
server:close()
fiber.sleep(0) -- workaround for gh-712: console.test.lua fails in Fedora
-- Check that admin console has been stopped
test:isnil(socket.tcp_connect("unix/", CONSOLE_SOCKET), "console.listen stopped")

-- Stop iproto
box.cfg{listen = ''}
os.remove(IPROTO_SOCKET)

local s = console.listen('127.0.0.1:0')
addr = s:name()
test:is(addr.family, 'AF_INET', 'console.listen uri support')
test:is(addr.host, '127.0.0.1', 'console.listen uri support')
test:isnt(addr.port, 0, 'console.listen uri support')
s:close()

local s = console.listen('console://unix/:'..CONSOLE_SOCKET)
addr = s:name()
test:is(addr.family, 'AF_UNIX', 'console.listen uri support')
test:is(addr.host, 'unix/', 'console.listen uri support')
test:is(addr.port, CONSOLE_SOCKET, 'console.listen uri support')
s:close()

test:check()

os.exit(0)
