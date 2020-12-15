#!/usr/bin/env tarantool

local ffi = require('ffi')

print[[
--------------------------------------------------------------------------------
-- #267: Bad exception catching
--------------------------------------------------------------------------------
]]

box.cfg{
    log="tarantool.log",
    memtx_memory=107374182,
}
local function pcalltest()
    local ERRMSG = "module 'some_invalid_module' not found"
    local status, msg = pcall(require, 'some_invalid_module')
    if status == false and msg ~= nil and msg:match(ERRMSG) ~= nil then
        return 'pcall is ok'
    else
        return 'pcall is broken'
    end
end

local status, msg = xpcall(pcalltest, function(msg)
    print('error handler:', msg)
end)
print('pcall inside xpcall:', status, msg)

local status, msg = pcall(function() error('some message') end)
print('pcall with Lua error():', status, msg:match('some message'))

local status, msg = pcall(function()
    box.error(box.error.ILLEGAL_PARAMS, 'some message')
end)
print('pcall with box.error():', status, msg)
print('pcall with box.error(): typeof', ffi.typeof(msg))
print('pcall with box.error(): .type', msg.type)
print('pcall with box.error(): .code', msg.code)
print('pcall with box.error(): .message', msg.message)
-- Tarantool 1.6 backward compatibility
print('pcall with box.error(): .match()', msg:match('some'))

print('pcall with no return:', select('#', pcall(function() end)))
print('pcall with multireturn:', pcall(function() return 1, 2, 3 end))
os.exit(0)
