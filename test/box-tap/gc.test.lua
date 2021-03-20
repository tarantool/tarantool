#!/usr/bin/env tarantool
local tap = require('tap')
local fiber = require('fiber')

box.cfg{}

local debug = type(box.error.injection) == "table"

-- check box.info.gc() is false if snapshot is not in progress
local test = tap.test('box.info.gc')
test:plan(2 + (debug and 1 or 0))


local gc = box.info.gc()
test:is(gc.checkpoint_is_in_progress, false, "checkpoint is not in progress")
test:is(gc.is_paused, false, "GC is not paused")

-- check box.info.gc() is true if snapshot is in progress
--
if debug then
    box.error.injection.set("ERRINJ_SNAP_COMMIT_DELAY", true)
    local snapshot_f  = function()
       box.snapshot()
    end
    fiber.create(snapshot_f)
    local gc = box.info.gc()
    test:is(gc.checkpoint_is_in_progress, true, "checkpoint is in progress")
    box.error.injection.set("ERRINJ_SNAP_COMMIT_DELAY", false)
end

test:check()

os.exit(0)
