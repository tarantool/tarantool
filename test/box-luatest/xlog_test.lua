local server = require('luatest.server')
local t = require('luatest')
local fio = require('fio')
local xlog = require('xlog')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
        cg.server = nil
    end
end)

local function xlog_at(cg, lsn)
    return fio.pathjoin(cg.server.workdir, string.format('%020d.xlog', lsn))
end

-- The newest xlog on disk -- the one currently open for writes.
local function newest_xlog(cg)
    local xlogs = fio.glob(fio.pathjoin(cg.server.workdir, '*.xlog'))
    table.sort(xlogs)
    return xlogs[#xlogs]
end

-- The first write creates the WAL: a fresh bootstrap leaves only the snapshot,
-- and the first DML opens the initial xlog.
--
-- NB: Use luatest.justrun instead of luatest.server to observe the state
-- between the bootstrap and the first DML operation. luatest.server performs
-- the first operation on its own to grant guest a code evaluation access (for
-- :exec()).
g.test_first_write_creates_xlog = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', string.dump(function()
        local fio = require('fio')
        local xlog = require('xlog')

        -- Fresh bootstrap: only the snapshot exists, no xlog yet.
        box.cfg()
        assert(#fio.glob('*.snap') == 1, 'an initial snapshot exists')
        assert(#fio.glob('*.xlog') == 0, 'no xlog before the first write')

        -- A first write creates the first xlog file.
        box.schema.space.create('tweedledum')
        local wals = fio.glob('*.xlog')
        assert(#wals == 1, 'the first write created an xlog')

        -- The xlog actually contains one operation.
        assert(xlog.pairs(wals[1]):length() == 1, 'xlog has one operation')

        os.exit(0)
    end))
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, {'main.lua'}, opts)
    t.assert_equals(res.exit_code, 0, {res.stdout, res.stderr})
end

-- A clean shutdown opens a new, empty xlog stamped with the final vclock and
-- leaves it on disk.
g.test_clean_shutdown_opens_new_xlog = function(cg)
    cg.server = server:new()
    cg.server:start()

    -- NB: A freshly bootstrapped instance is special, the shutdown doesn't
    -- produce 00..00.xlog. We want to verify an instance with some operations,
    -- not a fresh one, so ensure that :start() above already wrote something
    -- into the database (it grants guest privileges). That's why we expect
    -- LSN > 0 here.
    local lsn = cg.server:exec(function() return box.info.lsn end)
    t.assert_gt(lsn, 0)
    cg.server:stop()

    -- The shutdown opened a fresh xlog named by the final vclock; it holds no
    -- rows (just a header) and stays on disk.
    local wal = xlog_at(cg, lsn)
    t.assert_equals(#fio.glob(wal), 1, 'a new xlog is opened on clean shutdown')
    t.assert_equals(xlog.pairs(wal):length(), 0,
                    'the new xlog is empty (a header only)')
end

-- An incomplete last record (a crash mid-write) is tolerated on recovery: the
-- intact records are replayed and the incomplete one is dropped.
g.test_recovery_tolerates_incomplete_last_record = function(cg)
    cg.server = server:new()
    cg.server:start()

    cg.server:exec(function()
        local s = box.schema.space.create('tweedledum')
        s:create_index('primary', {type = 'hash'})
        s:insert({4, 'fourth tuple'})
        s:insert({5, 'Unfinished record'})
    end)

    -- Crash so the records stay in the last, unclosed xlog, then chop its last
    -- byte to make the final record incomplete.
    cg.server.process:kill('KILL')
    cg.server:stop()
    local wal = newest_xlog(cg)
    local fh = fio.open(wal, {'O_WRONLY'})
    fh:truncate(fio.stat(wal).size - 1)
    fh:close()

    -- Recovery replays the intact records and drops the incomplete tail.
    cg.server:start()
    cg.server:exec(function()
        t.assert_equals(box.space.tweedledum:get({4}), {4, 'fourth tuple'},
                        'the intact record recovered')
        t.assert_equals(box.space.tweedledum:get({5}), nil,
                        'the incomplete record was dropped')
    end)
end

-- A .snap.inprogress (a snapshot still being written) is skipped by the
-- directory scan: recovery falls back to the last complete snapshot.
g.test_snap_inprogress_is_ignored = function(cg)
    cg.server = server:new()
    cg.server:start()

    -- Preamble: build two complete snapshots, each after a write, so the newer
    -- one can be moved to .snap.inprogress while an earlier snapshot remains
    -- intact.
    local baseline_lsn, in_progress_lsn = cg.server:exec(function()
        local s = box.schema.space.create('tweedledum')
        s:create_index('primary')

        -- Baseline snapshot.
        s:insert({1})
        box.snapshot()
        local baseline_lsn = box.info.lsn

        -- In-progress snapshot.
        s:insert({2})
        box.snapshot()
        local in_progress_lsn = box.info.lsn

        return baseline_lsn, in_progress_lsn
    end)
    cg.server:stop()

    -- Move the latest snapshot to .snap.inprogress and drop the xlogs.
    --
    -- So only the baseline and the in-progress snapshots stay there and
    -- recovery uses the baseline.
    local snap = fio.pathjoin(cg.server.workdir,
                              string.format('%020d.snap', in_progress_lsn))
    fio.rename(snap, snap .. '.inprogress')
    for _, glob in ipairs({'*.xlog', '*.vylog'}) do
        for _, f in ipairs(fio.glob(fio.pathjoin(cg.server.workdir, glob))) do
            fio.unlink(f)
        end
    end

    -- Check: recovery ignores the .inprogress and comes up at the earlier one.
    cg.server:start()
    cg.server:exec(function(baseline_lsn)
        t.assert_equals(box.info.lsn, baseline_lsn)
        t.assert_equals(box.space.tweedledum:get({1}), {1})
        t.assert_equals(box.space.tweedledum:get({2}), nil)
    end, {baseline_lsn})
end
