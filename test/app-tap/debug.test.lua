#!/usr/bin/env tarantool

local TNTBIN = 'tarantool'

local ok, test_run = pcall(require, 'test_run')
test_run = ok and test_run.new() or nil

local tests = {
    "print(debug.sourcefile())",
    "print(debug.sourcedir())",
    "print(debug.__file__)",
    "print(debug.__dir__)",
    "print(require('net.box').self:call('debug.sourcefile'))",
    "print(require('net.box').self:call('debug.sourcedir'))",
    "fn = function() return debug.__file__ end; print(require('net.box').self:call('fn'))",
    "fn = function() return debug.__dir__ end; print(require('net.box').self:call('fn'))",
    "fn = function() local res = debug.sourcefile(); return res end; print(require('net.box').self:call('fn'))",
    "fn = function() local res = debug.sourcedir(); return res end; print(require('net.box').self:call('fn'))",
    "print(loadstring('return debug.sourcefile()')())",
    "print(loadstring('return debug.sourcedir()')())",
    "print(loadstring('return debug.__file__')())",
    "print(loadstring('return debug.__dir__')())",
    "print(loadstring('local res = debug.sourcefile(); return res')())",
    "print(loadstring('local res = debug.sourcedir(); return res')())",
}

print('=========================================')
print('When running netbox call on remote server')
print('=========================================')

local script_path = 'app-tap/debug/server.lua'
local cmd = ("create server remote with script='%s', return_listen_uri=True"):format(script_path)
local listen_uri = test_run:cmd(cmd)
test_run:cmd("start server remote")

-- Connecting to the server
local netbox = require('net.box')
local addr = string.format(listen_uri)
local conn = netbox.connect(addr)
assert(conn:ping(), 'Ping to the server must work')

local result, err = conn:call('debug.sourcefile')
print(('debug.sourcefile() => %s; %s'):format(tostring(result), tostring(err)))
assert(result == box.NULL, 'debug.sourcefile() returns box.NULL')
assert(err == nil, 'debug.sourcefile() returns no error')

local result, err = conn:call('debug.sourcedir')
print(('debug.sourcedir() => %s; %s'):format(tostring(result), tostring(err)))
assert(result == '.', 'debug.sourcedir() returns "."')
assert(err == nil, 'debug.sourcedir() returns no error')

test_run:cmd("stop server remote")
test_run:cmd("cleanup server remote")
test_run:cmd("delete server remote")

print('==================================')
print('When running lua code from console')
print('==================================')
-- debug.sourcefile() returns cwd when running within console
for _, test in ipairs(tests) do
    local cmd = string.format('%s -e "%s; os.exit(0)"', TNTBIN, test)
    print('Exec: '..cmd)
    io.flush()
    assert(os.execute(cmd) == 0, string.format('cmd: "%s" must execute successfully', cmd))
end

local fio = require('fio')
local dirname = 'debug'
local filename = fio.pathjoin(dirname, 'test.lua')
local dirstat = fio.stat(dirname)
if not dirstat then
    fio.mkdir(dirname)
    dirstat = fio.stat(dirname)
end
assert(dirstat:is_dir(), dirname..' must be a directory')

local cmd = TNTBIN..' '..filename
print('======================================')
print('When running lua code from script file')
print('======================================')
print('Exec: '..cmd)
print('==============================')
io.flush()
for _, test in ipairs(tests) do
    local file = fio.open(filename, {'O_CREAT', 'O_WRONLY', 'O_TRUNC'}, tonumber('644',8))
    file:write(test)
    file:close()
    print('Source: '..test)
    io.flush()
    assert(os.execute(cmd) == 0, string.format('cmd: "%s" must execute successfully', cmd))
end

os.remove(filename)
fio.rmdir(dirname)
