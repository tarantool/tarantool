#!/usr/bin/env tarantool

local tap = require('tap')
local console = require('console')
local socket = require('socket')
local yaml = require('yaml')
local fiber = require('fiber')

local CONSOLE_SOCKET = '/tmp/tarantool-test-console.sock'
local IPROTO_SOCKET = '/tmp/tarantool-test-iproto.sock'
os.remove(CONSOLE_SOCKET)
os.remove(IPROTO_SOCKET)

box.cfg{
    listen=IPROTO_SOCKET;
    slab_alloc_arena=0.1,
    logger="tarantool.log",
}

--
local EOL = "\n%.%.%.\n"

test = tap.test("console")

test:plan(24)

-- Start console and connect to it
local server = console.listen(CONSOLE_SOCKET)
test:ok(server ~= nil, "console.listen started")
local client = socket.tcp_connect("unix/", CONSOLE_SOCKET)
test:ok(client ~= nil, "connect to console")

-- Execute some command
client:write("1\n")
test:is(yaml.decode(client:read(EOL))[1], 1, "eval")

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

-- Connect to iproto console (CALL)
client:write(string.format("require('console').connect('unix/', '/')\n"))
-- error: Connection is not established
test:ok(yaml.decode(client:read(EOL))[1].error:find('not established'),
    'remote network error')

client:write(string.format("require('console').connect('unix/', '%s')\n",
    IPROTO_SOCKET))
-- error: Execute access denied for user 'guest' to function 'dostring
test:ok(yaml.decode(client:read(EOL))[1].error:find('access denied'),
    'remote access denied')

-- Add permissions to execute `dostring` for `guest`
box.schema.func.create('dostring')
box.schema.user.grant('guest', 'execute', 'function', 'dostring')

client:write(string.format("require('console').connect('unix/', '%s')\n",
    IPROTO_SOCKET))
test:is(yaml.decode(client:read(EOL)), '', "remote connect")

-- Execute some command
client:write("require('fiber').id()\n")
local fid2 = yaml.decode(client:read(EOL))[1]
test:isnt(fid1, fid2, "remote eval")

test:is(state.remote.host, "unix/", "remote state.remote.host")
test:is(state.remote.port, IPROTO_SOCKET, "remote state.remote.port")
test:is(state.prompt, string.format("%s:%s", "unix/", IPROTO_SOCKET),
        "remote state.prompt")

-- Disconnect from iproto
client:write("~.\n")
-- Check that iproto console has been disconnected
client:write("require('fiber').id()\n")
local fid1x = yaml.decode(client:read(EOL))[1]
test:is(fid1, fid1x, "remote disconnect")

-- Disconect from console
client:shutdown()
client:write('')
client:close()

-- Stop console
server:shutdown()
server:close()
-- Check that admon console has been stopped
test:isnil(socket.tcp_connect("unix/", CONSOLE_SOCKET), "console.listen stopped")

-- Stop iproto (not implemented yet)
-- box.cfg{listen = nil}
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
