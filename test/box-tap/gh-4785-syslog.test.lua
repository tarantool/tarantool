#!/usr/bin/env tarantool

local socket = require('socket')
local log = require('log')
local fio = require('fio')
local tap = require('tap')

-- A unix socket to read log entries from it.
local path = fio.pathjoin(fio.cwd(), 'syslog.sock')
local unix_socket = socket('AF_UNIX', 'SOCK_DGRAM', 0)
os.remove(path)
unix_socket:bind('unix/', path)

local identity = 'tarantool'
box.cfg{
    log = ('syslog:server=unix:%s,identity=%s'):format(path, identity),
}

-- Syslog format:
--
-- <PRI><TIMESTAMP> IDENTITY[PID]: CORD/FID/FILE FACILITY>
local patterns = {
    '<%d+>',                         -- PRI
    '%u%l%l  ?%d?%d %d%d:%d%d:%d%d', -- TIMESTAMP
    identity,                        -- IDENTITY
    '%[%d+%]',                       -- PID
    '[%l%d]+',                       -- CORD
    '%d+',                           -- FID
    '[%l%d-_.]+',                    -- FILE
    '%u',                            -- FACILITY
}
local pattern = ('%s%s %s%s: %s/%s/%s %s>'):format(unpack(patterns))

local test = tap.test('gh-4785-syslog')
test:plan(4)

-- Verify all log entries we have after box.cfg().
local ok = true
local logs = {}
while true do
    local entry = unix_socket:recv(100)
    if entry == nil then break end
    ok = ok and entry:match(pattern)
    table.insert(logs, entry)
end
test:ok(ok, 'box.cfg() log entries are in syslog format', {logs = logs})

-- Verify a log entry written by log.info().
log.info('hello')
local entry = unix_socket:recv(100)
test:like(entry, pattern, 'log.info() log entry is in syslog format',
          {logs = {entry}})

-- log.log_format('plain') is silently ignored.
local ok = pcall(log.log_format, 'plain')
test:ok(ok, "log.log_format('plain') is ignored with syslog")

-- Verify log format again after log.log_format().
log.info('world')
local entry = unix_socket:recv(100)
test:like(entry, pattern, 'log.info() log entry after log_format',
          {logs = {entry}})

-- Drop unix socket file.
unix_socket:close()
os.remove(path)

os.exit(test:check() and 0 or 1)
