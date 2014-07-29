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
    listen='unix://'..IPROTO_SOCKET;
    logger="tarantool.log"
}

--
local EOL = {"\n%.%.%.\n"}

-- Add permissions to execute `dostring` for `guest`
box.schema.func.create('dostring')
box.schema.user.grant('guest', 'execute', 'function', 'dostring')

test = tap.test("console")

test:plan(16)

-- Start console and connect to it
local server = console.listen(CONSOLE_SOCKET)
test:ok(server ~= nil, "console.listen started")
local client = socket.tcp_connect("unix/", CONSOLE_SOCKET)
test:ok(client ~= nil, "connect to console")

-- Execute some command
client:write("1\n")
test:is(yaml.decode(client:readline(EOL))[1], 1, "eval")

-- Check internal state of `console` module
client:write("require('fiber').id()\n")
local fid1 = yaml.decode(client:readline(EOL))[1]
local state = fiber.find(fid1).storage.console
local server_info = state.client:peer()
local client_info = state.client:name()
test:is(client_info.host, client_info.host, "state.socker:peer().host")
test:is(client_info.port, client_info.port, "state.socker:peer().port")
server_info = nil
client_info = nil

-- Check console.delimiter()
client:write("require('console').delimiter(';')\n")
test:is(yaml.decode(client:readline(EOL)), '', "set delimiter to ';'")
test:is(state.delimiter, ';', "state.delimiter is ';'")
client:write("require('console').delimiter();\n")
test:is(yaml.decode(client:readline(EOL))[1], ';', "get delimiter is ';'")
client:write("require('console').delimiter('');\n")
test:is(yaml.decode(client:readline(EOL)), '', "clear delimiter")

-- Connect to iproto console (CALL)
client:write(string.format("require('console').connect('unix/', '%s')\n",
    IPROTO_SOCKET))
test:is(yaml.decode(client:readline(EOL)), '', "remote connect")

-- Execute some command
client:write("require('fiber').id()\n")
local fid2 = yaml.decode(client:readline(EOL))[1]
test:isnt(fid1, fid2, "remote eval")

test:is(state.remote.host, "unix/", "remote state.remote.host")
test:is(state.remote.port, IPROTO_SOCKET, "remote state.remote.port")
test:is(state.prompt, string.format("%s:%s", "unix/", IPROTO_SOCKET),
        "remote state.prompt")

-- Disconnect from iproto
client:write("~.\n")
-- Check that iproto console has been disconnected
client:write("require('fiber').id()\n")
local fid1x = yaml.decode(client:readline(EOL))[1]
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

test:check()

-- Stop iproto (not implemented yet)
-- box.cfg{listen = nil}

os.remove(CONSOLE_SOCKET)
os.remove(IPROTO_SOCKET)
os.exit(0)
