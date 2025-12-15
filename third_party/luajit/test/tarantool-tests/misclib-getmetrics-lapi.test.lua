-- This is a part of tarantool/luajit testing suite.
-- Major portions taken verbatim or adapted from the LuaVela
-- testing suite.
-- Copyright (C) 2015-2019 IPONWEB Ltd.

local tap = require('tap')
local test = tap.test("lib-misc-getmetrics"):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

test:plan(10)

local MAXNINS = require('utils').jit.const.maxnins
local jit_opt_default = {
    3, -- level
    "hotloop=56",
    "hotexit=10",
    "minstitch=0",
}

-- Test Lua API.
test:test("base", function(subtest)
    subtest:plan(19)
    local metrics = misc.getmetrics()
    subtest:ok(metrics.strhash_hit >= 0)
    subtest:ok(metrics.strhash_miss >= 0)

    subtest:ok(metrics.gc_strnum >= 0)
    subtest:ok(metrics.gc_tabnum >= 0)
    subtest:ok(metrics.gc_udatanum >= 0)
    subtest:ok(metrics.gc_cdatanum >= 0)

    subtest:ok(metrics.gc_total >= 0)
    subtest:ok(metrics.gc_freed >= 0)
    subtest:ok(metrics.gc_allocated >= 0)

    subtest:ok(metrics.gc_steps_pause >= 0)
    subtest:ok(metrics.gc_steps_propagate >= 0)
    subtest:ok(metrics.gc_steps_atomic >= 0)
    subtest:ok(metrics.gc_steps_sweepstring >= 0)
    subtest:ok(metrics.gc_steps_sweep >= 0)
    subtest:ok(metrics.gc_steps_finalize >= 0)

    subtest:ok(metrics.jit_snap_restore >= 0)
    subtest:ok(metrics.jit_trace_abort >= 0)
    subtest:ok(metrics.jit_mcode_size >= 0)
    subtest:ok(metrics.jit_trace_num >= 0)
end)

test:test("gc-allocated-freed", function(subtest)
    subtest:plan(1)

    -- Force up garbage collect all dead objects (even those
    -- resurrected or postponed for custom finalization).
    repeat
        local count = collectgarbage("count")
        collectgarbage("collect")
    until collectgarbage("count") == count

    -- Bump getmetrics table and string keys allocation.
    misc.getmetrics()

    -- Remember allocated size for getmetrics table.
    local old_metrics = misc.getmetrics()

    collectgarbage("collect")

    local new_metrics = misc.getmetrics()

    local getmetrics_alloc = new_metrics.gc_allocated - old_metrics.gc_allocated

    -- Do not use test:ok to avoid extra allocated/freed objects.
    assert(getmetrics_alloc > 0, "count allocated table for getmetrics")
    old_metrics = new_metrics

    -- NB: Avoid operations that use internal global string buffer
    -- (such as concatenation, string.format, table.concat)
    -- while creating the string. Otherwise gc_freed/gc_allocated
    -- relations will not be so straightforward.
    -- luacheck: no unused
    local str = string.sub("Hello, world", 1, 5)
    collectgarbage("collect")

    new_metrics = misc.getmetrics()

    local diff_alloc = new_metrics.gc_allocated - old_metrics.gc_allocated
    local diff_freed = new_metrics.gc_freed - old_metrics.gc_freed

    assert(diff_alloc > getmetrics_alloc,
           "allocated str 'Hello' and table for getmetrics")
    assert(diff_freed == getmetrics_alloc,
           "freed old old_metrics")
    old_metrics = new_metrics

    str = string.sub("Hello, world", 8, -1)

    new_metrics = misc.getmetrics()

    diff_alloc = new_metrics.gc_allocated - old_metrics.gc_allocated
    diff_freed = new_metrics.gc_freed - old_metrics.gc_freed

    assert(diff_alloc > getmetrics_alloc,
            "allocated str 'world' and table for getmetrics")
    assert(diff_freed == 0, "nothing to free without collectgarbage")
    old_metrics = new_metrics
    collectgarbage("collect")

    new_metrics = misc.getmetrics()

    diff_alloc = new_metrics.gc_allocated - old_metrics.gc_allocated
    diff_freed = new_metrics.gc_freed - old_metrics.gc_freed

    assert(diff_alloc == getmetrics_alloc,
            "allocated last one table for getmetrics")
    assert(diff_freed > 2 * getmetrics_alloc,
            "freed str 'Hello' and 2 tables for getmetrics")
    subtest:ok(true, "no assetion failed")
end)

test:test("gc-steps", function(subtest)
    subtest:plan(24)

    -- Some garbage has already created before the next line,
    -- i.e. during frontend processing this chunk.
    -- Let's put a full garbage collection cycle on top of that,
    -- and confirm that non-null values are reported (we are not
    -- yet interested in actual numbers):
    collectgarbage("collect")
    collectgarbage("stop")
    local oldm = misc.getmetrics()
    subtest:ok(oldm.gc_steps_pause > 0)
    subtest:ok(oldm.gc_steps_propagate > 0)
    subtest:ok(oldm.gc_steps_atomic > 0)
    subtest:ok(oldm.gc_steps_sweepstring > 0)
    subtest:ok(oldm.gc_steps_sweep > 0)
    -- Nothing to finalize, skipped. Tarantool may perform some
    -- finalize steps already, so just use the base test.
    subtest:ok(oldm.gc_steps_finalize >= 0)

    -- As long as we stopped the GC, consequent call
    -- should return the same values:
    local newm = misc.getmetrics()
    subtest:is(newm.gc_steps_pause - oldm.gc_steps_pause, 0)
    subtest:is(newm.gc_steps_propagate - oldm.gc_steps_propagate, 0)
    subtest:is(newm.gc_steps_atomic - oldm.gc_steps_atomic, 0)
    subtest:is(newm.gc_steps_sweepstring - oldm.gc_steps_sweepstring, 0)
    subtest:is(newm.gc_steps_sweep - oldm.gc_steps_sweep, 0)
    -- Nothing to finalize, skipped.
    subtest:is(newm.gc_steps_finalize - oldm.gc_steps_finalize, 0)
    oldm = newm

    -- Now the last phase: run full GC once and make sure that
    -- everything is being reported as expected:
    collectgarbage("collect")
    collectgarbage("stop")
    newm = misc.getmetrics()
    subtest:ok(newm.gc_steps_pause - oldm.gc_steps_pause == 1)
    subtest:ok(newm.gc_steps_propagate - oldm.gc_steps_propagate >= 1)
    subtest:ok(newm.gc_steps_atomic - oldm.gc_steps_atomic == 1)
    subtest:ok(newm.gc_steps_sweepstring - oldm.gc_steps_sweepstring >= 1)
    subtest:ok(newm.gc_steps_sweep  - oldm.gc_steps_sweep >= 1)
    -- Nothing to finalize, skipped.
    subtest:is(newm.gc_steps_finalize - oldm.gc_steps_finalize, 0)
    oldm = newm

    -- Now let's run three GC cycles to ensure that increment
    -- was not a lucky coincidence.
    collectgarbage("collect")
    collectgarbage("collect")
    collectgarbage("collect")
    collectgarbage("stop")
    newm = misc.getmetrics()
    subtest:ok(newm.gc_steps_pause - oldm.gc_steps_pause == 3)
    subtest:ok(newm.gc_steps_propagate - oldm.gc_steps_propagate >= 3)
    subtest:ok(newm.gc_steps_atomic - oldm.gc_steps_atomic == 3)
    subtest:ok(newm.gc_steps_sweepstring - oldm.gc_steps_sweepstring >= 3)
    subtest:ok(newm.gc_steps_sweep  - oldm.gc_steps_sweep >= 3)
    -- Nothing to finalize, skipped.
    subtest:is(newm.gc_steps_finalize - oldm.gc_steps_finalize, 0)
end)

test:test("objcount", function(subtest)
    subtest:plan(5)
    local ffi = require("ffi")

    jit.opt.start(0)

    -- Remove all dead objects.
    collectgarbage("collect")

    local old_metrics = misc.getmetrics()

    local placeholder = {
        str = {},
        tab = {},
        udata = {},
        cdata = {},
    }

    -- Separate objects creations to separate jit traces.
    for _ = 1, 1000 do
        table.insert(placeholder.str, tostring(_))
    end

    for _ = 1, 1000 do
        table.insert(placeholder.tab, {_})
    end

    for _ = 1, 1000 do
        table.insert(placeholder.udata, newproxy())
    end

    for _ = 1, 1000 do
        -- Check counting of VLA/VLS/aligned cdata.
        table.insert(placeholder.cdata, ffi.new("char[?]", 4))
    end

    for _ = 1, 1000 do
        -- Check counting of non-VLA/VLS/aligned cdata.
        table.insert(placeholder.cdata, ffi.new("uint64_t", _))
    end

    placeholder = nil -- luacheck: no unused
    collectgarbage("collect")
    local new_metrics = misc.getmetrics()

    -- Check that amount of objects not increased.
    subtest:is(new_metrics.gc_strnum, old_metrics.gc_strnum,
               "strnum don't change")
    -- When we call getmetrics, we create table for metrics first.
    -- So, when we save old_metrics there are x + 1 tables,
    -- when we save new_metrics there are x + 2 tables, because
    -- old table hasn't been collected yet (it is still
    -- reachable).
    subtest:is(new_metrics.gc_tabnum - old_metrics.gc_tabnum, 1,
               "tabnum don't change")
    subtest:is(new_metrics.gc_udatanum, old_metrics.gc_udatanum,
               "udatanum don't change")
    subtest:is(new_metrics.gc_cdatanum, old_metrics.gc_cdatanum,
               "cdatanum don't change")

    -- gc_cdatanum decrement test.
    -- See https://github.com/tarantool/tarantool/issues/5820.
    local function nop() end
    local cdatanum_old = misc.getmetrics().gc_cdatanum
    ffi.gc(ffi.cast("void *", 0), nop)
    -- Does not collect the cdata, but resurrects the object and
    -- removes LJ_GC_CDATA_FIN flag.
    collectgarbage()
    -- Collects the cdata.
    collectgarbage()
    subtest:is(misc.getmetrics().gc_cdatanum, cdatanum_old,
               "cdatanum is decremented correctly")

    -- Restore default jit settings.
    jit.opt.start(unpack(jit_opt_default))
end)

test:test("snap-restores-direct-loop", function(subtest)
    -- Compiled loop with a direct exit to the interpreter.
    subtest:plan(1)

    jit.opt.start(0, "hotloop=1")

    local old_metrics = misc.getmetrics()

    local sum = 0
    for i = 1, 20 do
        sum = sum + i
    end

    local new_metrics = misc.getmetrics()

    -- A single snapshot restoration happened on loop finish:
    subtest:is(new_metrics.jit_snap_restore - old_metrics.jit_snap_restore, 1)

    -- Restore default jit settings.
    jit.opt.start(unpack(jit_opt_default))
end)

test:test("snap-restores-loop-side-exit-non-compiled", function(subtest)
    -- Compiled loop with a side exit which does not get compiled.
    subtest:plan(1)

    jit.opt.start(0, "hotloop=1", "hotexit=2", ("minstitch=%d"):format(MAXNINS))

    local function foo(i)
        -- math.fmod is not yet compiled!
        return i <= 5 and i or math.fmod(i, 11)
    end

    local old_metrics = misc.getmetrics()
    local sum = 0
    for i = 1, 10 do
        sum = sum + foo(i)
    end

    local new_metrics = misc.getmetrics()

    -- Side exits from the root trace could not get compiled.
    subtest:is(new_metrics.jit_snap_restore - old_metrics.jit_snap_restore, 5)

    -- Restore default jit settings.
    jit.opt.start(unpack(jit_opt_default))
end)

test:test("snap-restores-loop-side-exit", function(subtest)
    -- Compiled loop with a side exit which gets compiled.
    subtest:plan(1)

    -- Optimization level is important here as `loop` optimization
    -- may unroll the loop body and insert +1 side exit.
    jit.opt.start(0, "hotloop=1", "hotexit=5")

    local function foo(i)
        return i <= 10 and i or tostring(i)
    end

    local old_metrics = misc.getmetrics()
    local sum = 0
    for i = 1, 20 do
        sum = sum + foo(i)
    end

    local new_metrics = misc.getmetrics()

    -- 5 side exits to the interpreter before trace gets hot
    -- and compiled
    -- 1 side exit on loop end
    subtest:is(new_metrics.jit_snap_restore - old_metrics.jit_snap_restore, 6)

    -- Restore default jit settings.
    jit.opt.start(unpack(jit_opt_default))
end)

test:test("snap-restores-scalar", function(subtest)
    -- Compiled scalar trace with a direct exit to the
    -- interpreter.
    subtest:plan(2)

    -- For calls it will be 2 * hotloop (see lj_dispatch.{c,h}
    -- and hotcall@vm_*.dasc).
    jit.opt.start(3, "hotloop=2", "hotexit=3")

    local function foo(i)
        return i <= 15 and i or tostring(i)
    end

    local old_metrics = misc.getmetrics()

    foo(1)  -- interp only
    foo(2)  -- interp only
    foo(3)  -- interp only
    foo(4)  -- compile trace during this call
    foo(5)  -- follow the trace
    foo(6)  -- follow the trace
    foo(7)  -- follow the trace
    foo(8)  -- follow the trace
    foo(9)  -- follow the trace
    foo(10) -- follow the trace

    local new_metrics = misc.getmetrics()

    -- No exits triggering snap restore so far: snapshot
    -- restoration was inlined into the machine code.
    subtest:is(new_metrics.jit_snap_restore - old_metrics.jit_snap_restore, 0)
    -- XXX: obtain actual metrics to avoid side effects that are
    -- caused by Lua code and JIT engine fine tuning above.
    old_metrics = misc.getmetrics()

    -- Simply 2 side exits from the trace:
    foo(20)
    foo(21)

    new_metrics = misc.getmetrics()
    subtest:is(new_metrics.jit_snap_restore - old_metrics.jit_snap_restore, 2)

    -- Restore default jit settings.
    jit.opt.start(unpack(jit_opt_default))
end)

test:test("strhash", function(subtest)
    subtest:plan(1)

    local old_metrics = misc.getmetrics()

    local new_metrics = misc.getmetrics()
    -- Do not use test:ok to avoid extra strhash hits/misses.
    assert(new_metrics.strhash_hit - old_metrics.strhash_hit == 19)
    assert(new_metrics.strhash_miss - old_metrics.strhash_miss == 0)
    old_metrics = new_metrics

    local _ = "strhash".."_hit"

    new_metrics = misc.getmetrics()
    assert(new_metrics.strhash_hit - old_metrics.strhash_hit == 20)
    assert(new_metrics.strhash_miss - old_metrics.strhash_miss == 0)
    old_metrics = new_metrics

    new_metrics = misc.getmetrics()
    assert(new_metrics.strhash_hit - old_metrics.strhash_hit == 19)
    assert(new_metrics.strhash_miss - old_metrics.strhash_miss == 0)
    old_metrics = new_metrics

    local _ = "new".."string"

    new_metrics = misc.getmetrics()
    assert(new_metrics.strhash_hit - old_metrics.strhash_hit == 19)
    assert(new_metrics.strhash_miss - old_metrics.strhash_miss == 1)
    subtest:ok(true, "no assertion failed")
end)

test:test("tracenum-base", function(subtest)
    subtest:plan(3)

    jit.flush()
    collectgarbage("collect")
    local metrics = misc.getmetrics()
    subtest:is(metrics.jit_trace_num, 0)

    local sum = 0
    for i = 1, 100 do
        sum = sum + i
    end

    metrics = misc.getmetrics()
    subtest:is(metrics.jit_trace_num, 1)

    jit.flush()
    collectgarbage("collect")

    metrics = misc.getmetrics()
    subtest:is(metrics.jit_trace_num, 0)
end)

test:done(true)
