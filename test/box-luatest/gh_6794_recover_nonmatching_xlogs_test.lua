local server = require('luatest.server')
local t = require('luatest')
local fio = require('fio')

local g = t.group()

g.before_all(function(cg)
    -- We need to create such an xlog and a snap that the xlog has data both
    -- before and after the snap. For example, 0...0.xlog and 0...02.snap, but
    -- the xlog contains entries with lsns 1, 2 and 3. This is not possible with
    -- one instance, because it rotates its xlog while making a snapshot. So,
    -- create two instances with identical uuids, and make one of them generate
    -- the needed snapshot and the other - the needed xlog. Then move the files
    -- into one folder.
    cg.box_cfg = {instance_uuid = '6f9019aa-2821-4f34-905b-51aed43def47'}
    cg.snap = server:new{alias = 'snapshot', box_cfg = cg.box_cfg}
    cg.xlog = server:new{alias = 'xlog', box_cfg = cg.box_cfg}

    cg.snap:start()
    cg.snap:exec(function()
        t.assert_equals(box.info.id, 1, 'Server id is correct')
        box.space._schema:replace{'aaa'}
        t.assert_equals(box.info.lsn, 2, 'Exactly two entries are written')
        box.snapshot()
    end)
    cg.snap:stop()
    cg.snap_path = fio.pathjoin(cg.snap.workdir, '00000000000000000002.snap')
    t.assert_equals(#fio.glob(cg.snap_path), 1, 'Snapshot is created')

    cg.xlog:start()
    cg.xlog:exec(function()
        t.assert_equals(box.info.id, 1, 'Server id is correct')
        box.space._schema:replace{'aaa'}
        box.space._schema:replace{'bbb'}
        t.assert_equals(box.info.lsn, 3, 'Exactly three entries are written')
    end)
    cg.xlog:stop()
    cg.xlog_path = fio.pathjoin(cg.xlog.workdir, '00000000000000000000.xlog')
    t.assert_equals(#fio.glob(cg.xlog_path), 1, 'Xlog is created')

    cg.tempdir = fio.tempdir()
    fio.copyfile(cg.snap_path, cg.tempdir)
    fio.copyfile(cg.xlog_path, cg.tempdir)
end)

g.after_all(function(cg)
    cg.snap:drop()
    cg.xlog:drop()
    fio.rmtree(cg.tempdir)
end)

g.before_test('test_panic_without_force_recovery', function()
    g.server = server:new({alias = 'master-test_panic',
                           datadir = g.tempdir})
    g.server:start({wait_until_ready = false})
end)

g.after_test("test_panic_without_force_recovery", function()
    g.server:drop()
end)

g.before_test('test_ignore_with_force_recovery', function()
    g.server = server:new({alias = 'master-test_ignore',
                           datadir = g.tempdir,
                           box_cfg = {force_recovery = true}})
    g.server:start()
end)

g.after_test("test_ignore_with_force_recovery", function()
    g.server:drop()
end)

local mismatch_msg = "Replicaset vclock {.*} doesn't match recovered data {.*}"

g.test_panic_without_force_recovery = function()
    t.helpers.retrying({}, function()
        local msg = "Can't proceed. " .. mismatch_msg
        local filename = fio.pathjoin(g.server.workdir, g.server.alias..'.log')
        t.assert(g.server:grep_log(msg, nil, {filename = filename}))
    end)
end

g.test_ignore_with_force_recovery = function()
    t.helpers.retrying({}, function()
        local msg = mismatch_msg .. ": ignoring, because 'force_recovery' "..
                    "configuration option is set."
        t.assert(g.server:grep_log(msg))
    end)
    g.server:exec(function()
        t.assert_equals(box.info.lsn, 3, 'Lsn is correct')
        t.assert(box.space._schema:get{'aaa'}, 'First tuple is recovered')
        t.assert(box.space._schema:get{'bbb'}, 'Second tuple is recovered')
        t.assert_equals(box.info.status, 'running', 'The server is recovered')
        t.assert(not box.info.ro, 'Server is writable')
    end)
end
