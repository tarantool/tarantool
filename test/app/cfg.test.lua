#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('cfg')
test:plan(13)

--------------------------------------------------------------------------------
-- Invalid values
--------------------------------------------------------------------------------

test:is(type(box.cfg), 'function', 'box is not started')

local function invalid(name, val)
    local status, result = pcall(box.cfg, {[name]=val})
    test:ok(not status and result:match('Incorrect option'), 'invalid '..name)
end

invalid('replication_source', '//guest@localhost:3301')
invalid('wal_mode', 'invalid')
invalid('rows_per_wal', -1)

test:is(type(box.cfg), 'function', 'box is not started')

--------------------------------------------------------------------------------
-- All box members must raise an exception on access if box.cfg{} wasn't called
--------------------------------------------------------------------------------

local box = require('box')
local function testfun()
    return type(box.space)
end

local status, result = pcall(testfun)

test:ok(not status and result:match('Please call box.cfg{}'),
    'exception on unconfigured box')

box.cfg{
    logger="tarantool.log",
    slab_alloc_arena=0.1,
    wal_mode = "", -- "" means default value
}

status, result = pcall(testfun)
test:ok(status and result == 'table', 'configured box')

--------------------------------------------------------------------------------
-- gh-534: Segmentation fault after two bad wal_mode settings
--------------------------------------------------------------------------------

test:is(box.cfg.wal_mode, "write", "cfg.wal_mode default value")
box.cfg{wal_mode = ""}
test:is(box.cfg.wal_mode, "write", "cfg.wal_mode default value")
box.cfg{wal_mode = "none"}
test:is(box.cfg.wal_mode, "none", "cfg.wal_mode change")
-- "" or NULL resets option to default value
box.cfg{wal_mode = ""}
test:is(box.cfg.wal_mode, "write", "cfg.wal_mode default value")
box.cfg{wal_mode = "none"}
test:is(box.cfg.wal_mode, "none", "cfg.wal_mode change")
box.cfg{wal_mode = require('msgpack').NULL}
test:is(box.cfg.wal_mode, "write", "cfg.wal_mode default value")

test:check()
os.exit(0)
