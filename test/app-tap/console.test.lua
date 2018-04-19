#!/usr/bin/env tarantool

local tap = require('tap')
local console = require('console')
local socket = require('socket')
local yaml = require('yaml')
local fiber = require('fiber')
local ffi = require('ffi')
local log = require('log')
local fio = require('fio')

-- Suppress console log messages
log.level(4)
local CONSOLE_SOCKET = fio.pathjoin(fio.cwd(), 'tarantool-test-console.sock')
local IPROTO_SOCKET = fio.pathjoin(fio.cwd(), 'tarantool-test-iproto.sock')
os.remove(CONSOLE_SOCKET)
os.remove(IPROTO_SOCKET)

--
local EOL = "\n...\n"

test = tap.test("console")

test:plan(60)

-- Start console and connect to it
local server = console.listen(CONSOLE_SOCKET)
test:ok(server ~= nil, "console.listen started")
local client = socket.tcp_connect("unix/", CONSOLE_SOCKET)
local handshake = client:read{chunk = 128}
test:ok(string.match(handshake, '^Tarantool .*console') ~= nil, 'Handshake')
test:ok(client ~= nil, "connect to console")

--
-- gh-2677: box.session.push, text protocol support.
--
client:write('box.session.push(200)\n')
test:is(client:read(EOL), "---\n- null\n- Session 'console' does not support push()\n...\n", "push does not work")

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
    memtx_memory = 107374182,
    log="tarantool.log",
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
client:write("require('net.box').connect('unix/', '"..IPROTO_SOCKET.."')\n")
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

--
-- gh-1938: on_connect/on_disconnect/on_auth triggers
--
local session_id = box.session.id()
local triggers_ran = 0

local function console_on_connect()
    test:is(box.session.user(), "admin", "on_connect session.user()")
    test:like(box.session.peer(), "unix", "on_connect session.peer()")
    test:isnt(box.session.id(), session_id, "on_connect session.id()")
    triggers_ran = triggers_ran + 1
end

local function console_on_disconnect()
    test:is(box.session.user(), "admin", "on_disconnect session.user()")
    test:isnt(box.session.id(), session_id, "on_disconnect session.id()")
    triggers_ran = triggers_ran + 1
end

local function console_on_auth(username, success)
    test:is(box.session.user(), "admin", "on_auth session.user()")
    test:like(box.session.peer(), "unix", "on_auth session.peer()")
    test:isnt(box.session.id(), session_id, "on_auth session.id()")
    test:is(username, "admin", "on_auth argument")
    test:is(success, true, "on_auth argument 2")
    triggers_ran = triggers_ran + 1
end

box.session.on_connect(console_on_connect)
box.session.on_disconnect(console_on_disconnect)
box.session.on_auth(console_on_auth)

-- check on_connect/on_disconnect/on_auth triggers
local server = console.listen('console://unix/:'..CONSOLE_SOCKET)
client = socket.tcp_connect("unix/", CONSOLE_SOCKET)
_ = client:read(128)
client:write("1\n")
test:is(yaml.decode(client:read(EOL))[1], 1, "eval with triggers")
client:shutdown()
client:close()
while triggers_ran < 3 do fiber.yield() end

-- check on_auth with error()
local function console_on_auth_error()
    error("Authorization error")
    triggers_ran = triggers_ran + 1
end
box.session.on_auth(console_on_auth_error)

client = socket.tcp_connect("unix/", CONSOLE_SOCKET)
_ = client:read(128)
test:is(client:read(1024), "", "on_auth aborts connection")
client:close()
while triggers_ran < 4 do fiber.yield() end
test:is(triggers_ran, 4, "on_connect -> on_auth_error order")
box.session.on_auth(nil, console_on_auth_error)

--
-- gh-2642 "box.session.type()"
--
client = socket.tcp_connect("unix/", CONSOLE_SOCKET)
_ = client:read(128)
client:write("box.session.type();\n")
test:is(yaml.decode(client:read(EOL))[1], "console", "session type")
client:close()

server:close()

box.session.on_connect(nil, console_on_connect)
box.session.on_disconnect(nil, console_on_disconnect)
box.session.on_auth(nil, console_on_auth)
box.schema.user.drop('test')
box.schema.user.drop('test2')
session_id = nil
triggers_ran = nil
os.remove(CONSOLE_SOCKET)
os.remove(IPROTO_SOCKET)

test:check()

os.exit(0)
