local server = require('luatest.server')
local t = require('luatest')
local fio = require('fio')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({
        box_cfg = {
            force_recovery = true,
        },
    })
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

-- gh-167: force recovery must not get stuck on a gap in the LSN sequence caused
-- by a missing xlog. A normal recovery must stop at such an inconsistency;
-- force recovery reports the gap to the log and still applies the rows stored
-- after it.
g.test_force_recovery_ignores_lsn_gap = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('primary')
        s:insert({1, 'first tuple'})
        s:insert({2, 'second tuple'})
    end)

    -- The next xlog to be opened (on the restart below) will hold the {3}
    -- insert. Remember its path so it can be removed later to make the gap.
    local lsn = cg.server:exec(function() return box.info.lsn end)
    local wal = fio.pathjoin(cg.server.workdir,
                             string.format('%020d.xlog', lsn))

    -- Put {3} and {4} into their own xlogs by restarting in between.
    cg.server:restart()
    cg.server:exec(function()
        box.space.test:insert({3, 'third tuple'})
    end)
    cg.server:restart()
    cg.server:exec(function()
        box.space.test:insert({4, 'fourth tuple'})
    end)

    -- Drop the xlog that holds {3} and recover from the resulting gap.
    cg.server:stop()
    t.assert_equals(#fio.glob(wal), 1, 'the xlog to remove exists')
    fio.unlink(wal)
    cg.server:start()

    t.assert(cg.server:grep_log('ignoring a gap in LSN'))

    -- The tuple from the removed xlog is lost, the rest is recovered.
    cg.server:exec(function()
        t.assert_equals(box.space.test:select(), {
            {1, 'first tuple'},
            {2, 'second tuple'},
            {4, 'fourth tuple'},
        })
    end)
end

-- gh-716: force recovery used to loop forever if an xlog with inserts was
-- missing while a later xlog held deletes for the same, now absent, tuples. The
-- deletes match nothing and get no LSN, which used to break the gap handling. A
-- normal recovery stops at the gap; force recovery reports it and continues.
g.test_force_recovery_ignores_delete_for_lost_tuple = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('primary')
    end)

    -- The xlog opened on this restart will hold the inserts. Remember it.
    cg.server:restart()
    local lsn = cg.server:exec(function() return box.info.lsn end)
    local wal = fio.pathjoin(cg.server.workdir,
                             string.format('%020d.xlog', lsn))
    cg.server:exec(function()
        local s = box.space.test
        s:insert({1, 'first tuple'})
        s:insert({2, 'second tuple'})
        s:insert({3, 'third tuple'})
    end)

    -- Put the deletes into a separate xlog.
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.test
        s:delete({1})
        s:delete({2})
        s:delete({3})
    end)

    -- Remove the xlog with the inserts and recover from the resulting gap.
    cg.server:stop()
    t.assert_equals(#fio.glob(wal), 1, 'the xlog to remove exists')
    fio.unlink(wal)
    cg.server:start()

    t.assert(cg.server:grep_log('ignoring a gap in LSN'))

    -- The inserts are gone, the deletes matched nothing: the space is empty.
    cg.server:exec(function()
        t.assert_equals(box.space.test:select(), {})
    end)
end

-- https://bugs.launchpad.net/tarantool/+bug/1052018: a duplicate key error
-- while replaying an xlog must not stop recovery under force_recovery. The
-- error is logged and the conflicting row is skipped, so the row recovered
-- from the earlier (authoritative) xlog wins.
--
-- Two xlogs carrying inserts for the same keys are produced by writing the
-- first pair, moving that xlog aside, writing a conflicting pair (which lands
-- in a different xlog since recovery advanced the vclock past the missing
-- one), and then restoring the first xlog next to the second.
g.test_force_recovery_ignores_duplicate_key = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('primary')
        box.snapshot()
    end)

    -- The xlog opened by the snapshot holds the first pair of inserts.
    local lsn = cg.server:exec(function() return box.info.lsn end)
    local wal = fio.pathjoin(cg.server.workdir,
                             string.format('%020d.xlog', lsn))
    cg.server:exec(function()
        box.space.test:insert({1, 'first tuple'})
        box.space.test:insert({2, 'second tuple'})
    end)

    -- Move the first xlog aside.
    cg.server:stop()
    t.assert_equals(#fio.glob(wal), 1, 'the first xlog exists')
    local wal_saved = wal:gsub('%.xlog$', '_old.xlog')
    fio.rename(wal, wal_saved)

    -- Recover without it and write a conflicting pair for the same keys.
    cg.server:start()
    cg.server:exec(function()
        box.space.test:insert({1, 'third tuple'})
        box.space.test:insert({2, 'fourth tuple'})
    end)
    cg.server:stop()

    -- The conflicting inserts must have landed in a different xlog, otherwise
    -- restoring the first one below would just overwrite them and there would
    -- be no conflict to recover from.
    t.assert_equals(#fio.glob(wal), 0, 'the conflicting inserts went elsewhere')
    fio.rename(wal_saved, wal)

    -- Recovery replays the restored xlog first, then hits the duplicate keys
    -- in the other one and, thanks to force_recovery, skips them.
    cg.server:start()
    t.assert(cg.server:grep_log('Duplicate key exists in unique index'))
    cg.server:exec(function()
        t.assert_equals(box.space.test:get({1}), {1, 'first tuple'})
        t.assert_equals(box.space.test:get({2}), {2, 'second tuple'})
        t.assert_equals(box.space.test:len(), 2)
    end)
end
