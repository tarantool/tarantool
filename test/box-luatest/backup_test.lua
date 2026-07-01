local server = require('luatest.server')
local replica_set = require('luatest.replica_set')
local fio = require('fio')
local t = require('luatest')

local g = t.group()

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
    if cg.backup_dir ~= nil then
        fio.rmtree(cg.backup_dir)
    end
end)

local backup_files = function(cg, files)
    for _, file in ipairs(files) do
        local path_src = fio.pathjoin(cg.server.workdir, file)
        local path_dst = fio.pathjoin(cg.backup_dir, file)
        fio.copyfile(path_src, path_dst)
    end
end

local backup = function(cg, files)
    if cg.backup_dir ~= nil then
        fio.rmtree(cg.backup_dir)
    end
    cg.backup_dir = fio.tempdir()
    backup_files(cg, files)
end

local restore = function(cg)
    assert(cg.backup_dir)
    fio.rmtree(cg.server.workdir)
    fio.mkdir(cg.server.workdir)
    fio.copytree(cg.backup_dir, cg.server.workdir)
    fio.rmtree(cg.backup_dir)
    cg.backup_dir = nil
end

g.test_backup_rotated_xlog = function(cg)
    cg.server = server:new()
    cg.server:start()
    local files = cg.server:exec(function()
        box.cfg{}
        local s = box.schema.create_space('test')
        s:create_index('pk')
        for i = 1, 10 do
            s:insert({i})
        end
        box.snapshot()
        for i = 11, 20 do
            s:insert({i})
        end
        local files = box.backup.start()
        for i = 21, 30 do
            s:insert({i})
        end
        return files
    end)
    t.assert_equals(#files, 2)
    backup(cg, files)
    cg.server:exec(function()
        local s = box.space.test
        box.backup.stop()
        for i = 31, 40 do
            s:insert({i})
        end
    end)
    cg.server:stop()
    restore(cg)
    cg.server:start()
    cg.server:exec(function()
        local s = box.space.test
        local expected = {}
        for i = 1, 20 do
            table.insert(expected, {i})
        end
        t.assert_equals(s:select(), expected)
    end)
end

g.test_backup_many_xlogs = function(cg)
    cg.server = server:new({box_cfg = {wal_max_size = 16384}})
    cg.server:start()
    local files = cg.server:exec(function()
        local digest = require('digest')

        local s = box.schema.create_space('test')
        s:create_index('pk')
        for i = 1, 10 do
            s:insert({i})
        end
        box.snapshot()
        for i = 11, 20 do
            s:insert({i, digest.urandom(16384)})
        end
        local files = box.backup.start()
        for i = 21, 30 do
            s:insert({i, digest.urandom(16384)})
        end
        return files
    end)
    t.assert_equals(#files, 11)
    backup(cg, files)
    cg.server:stop()
    restore(cg)
    cg.server:start()
    cg.server:exec(function()
        local s = box.space.test
        t.assert_equals(s:count(), 20)
    end)
end

g.test_backup_no_xlogs = function(cg)
    cg.server = server:new()
    cg.server:start()
    local files = cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        for i = 1, 10 do
            s:insert({i})
        end
        box.snapshot()
        local files = box.backup.start()
        for i = 11, 20 do
            s:insert({i})
        end
        return files
    end)
    t.assert_equals(#files, 1)
    backup(cg, files)
    cg.server:stop()
    restore(cg)
    cg.server:start()
    cg.server:exec(function()
        local s = box.space.test
        t.assert_equals(s:count(), 10)
    end)
end

g.test_backup_xlogs_pinned = function(cg)
    cg.server = server:new({box_cfg = {checkpoint_count = 1}})
    cg.server:start()
    local files = cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        for i = 1, 10 do
            s:insert({i})
        end
        box.snapshot()
        for i = 11, 20 do
            s:insert({i})
        end
        local files = box.backup.start()
        box.snapshot()
        return files
    end)
    t.assert_equals(#files, 2)
    for _, file in ipairs(files) do
        local path = fio.pathjoin(g.server.workdir, file)
        t.assert(fio.path.exists(path))
    end
    backup(cg, files)
    cg.server:stop()
    restore(cg)
    cg.server:start()
    cg.server:exec(function()
        local s = box.space.test
        t.assert_equals(s:count(), 20)
    end)
end

g.test_backup_info = function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        -- Basic case.
        for i = 1, 10 do
            s:insert({i})
        end
        t.assert_equals(box.backup.info(), nil)
        local vclock = box.info.vclock
        local files = box.backup.start()
        for i = 11, 20 do
            s:insert({i})
        end
        t.assert_equals(box.backup.info(), {
            files = files,
            vclock = vclock,
            type = 'full',
            recovery_points = {},
        })
        box.backup.stop()
        t.assert_equals(box.backup.info(), nil)

        -- Case when there is no xlogs.
        box.snapshot()
        local vclock = box.info.vclock
        local files = box.backup.start()
        t.assert_equals(box.backup.info(), {
            files = files,
            vclock = vclock,
            type = 'full',
            recovery_points = {},
        })
        box.backup.stop()

        -- Case when backup is not based on latest snapshot.
        s:insert({21})
        box.snapshot()
        local files = box.backup.start(1)
        t.assert_equals(box.backup.info(), {
            files = files,
            vclock = vclock,
            type = 'full',
            recovery_points = {},
        })
        box.backup.stop()
    end)
end

g.after_test('test_backup_concurrent_info', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end)

g.test_backup_concurrent_info = function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.create_space('test')
        s:create_index('pk')

        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        fiber.create(s.insert, s, {1})
        fiber.create(box.backup.start)
        t.assert_equals(box.backup.info(), nil)
    end)
end

g.after_test('test_backup_concurrent_info', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end)

g.test_backup_concurrent_start = function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.create_space('test')
        s:create_index('pk')

        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        fiber.create(s.insert, s, {1})
        fiber.create(box.backup.start)
        local f = fiber.new(box.backup.start)
        f:set_joinable(true)
        fiber.yield()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok, err = f:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {
            type = 'ClientError',
            code = box.error.BACKUP_IN_PROGRESS,
        })
    end)
end

g.test_backup_from_vclock = function(cg)
    cg.server = server:new()
    cg.server:start()
    local info_0 = cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        for i = 1, 10 do
            s:insert({i})
        end
        box.backup.start()
        for i = 11, 20 do
            s:insert({i})
        end
        return box.backup.info()
    end)
    backup(cg, info_0.files)
    local info_1 = cg.server:exec(function(prev_vclock)
        box.backup.stop()
        local vclock = box.info.vclock
        local files = box.backup.start({from_vclock = prev_vclock})
        local s = box.space.test
        for i = 21, 30 do
            s:insert({i})
        end
        t.assert_equals(box.backup.info(), {
            files = files,
            vclock = vclock,
            prev_vclock = prev_vclock,
            type = 'incremental',
            recovery_points = {},
        })
        return box.backup.info()
    end, {info_0.vclock})
    t.assert_items_exclude(info_0.files, info_1.files)
    backup_files(cg, info_1.files)
    cg.server:stop()
    restore(cg)
    cg.server:start()
    cg.server:exec(function()
        local s = box.space.test
        local expected = {}
        for i = 1, 20 do
            table.insert(expected, {i})
        end
        t.assert_equals(s:select(), expected)

        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'expected number or table as 1 argument',
        }, box.backup.start, function() end)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'invalid from_vclock',
        }, box.backup.start, {from_vclock = 1})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'XLOG_NOT_FOUND',
            vclock = '{1: 100500}',
        }, box.backup.start, {from_vclock = {100500}})
        -- The {1, 100500} is not comparable with some for xlog vclocks.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'XLOG_NOT_FOUND',
            vclock = '{1: 1, 2: 100500}',
        }, box.backup.start, {from_vclock = {1, 100500}})

        -- Empty incremental backup.
        local vclock = box.info.vclock
        t.assert_equals(box.backup.start({from_vclock = vclock}), {})
        t.assert_equals(box.backup.info(), {
            files = {},
            vclock = vclock,
            prev_vclock = vclock,
            type = 'incremental',
            recovery_points = {},
        })
    end)
end

g.test_backup_ttl = function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local compat = require('compat')
        local fiber = require('fiber')

        -- Error cases.
        local err = {
            type = 'IllegalParams',
            message = 'invalid ttl',
        }
        t.assert_error_covers(err, box.backup.start, {ttl = 'unexpected'})
        t.assert_error_covers(err, box.backup.start, {ttl = 0})
        t.assert_error_covers(err, box.backup.start, {ttl = -1})

        -- TTL functionality.
        box.backup.start({ttl = 0.01})
        t.assert_not_equals(box.backup.info(), nil)
        t.helpers.retrying({}, function()
            t.assert_equals(box.backup.info(), nil)
        end)
        box.backup.start({ttl = 7000})
        t.assert_equals(box.backup.info().expires_at, fiber.time() + 7000)
        box.backup.stop()

        -- TTL compat.
        compat.box_backup_default_ttl = 'old'
        box.backup.start()
        t.assert_equals(box.backup.info().expires_at, nil)
        box.backup.stop()
        compat.box_backup_default_ttl = 'new'
        box.backup.start()
        t.assert_equals(box.backup.info().expires_at, fiber.time() + 3600)
        box.backup.stop()
    end)
end

-- The test requires fresh server with empty vclock.
g.test_recovery_point_list_filtering_empty_vclock = function(cg)
    cg.server = server:new({})
    cg.server:start()
    cg.server:exec(function()
        box.backup.start()
        for _ = 1, 10 do
            box.backup.recovery_point.create()
        end
        -- Covers case when backup begin vclock is empty.
        t.assert_equals(box.backup.info().recovery_points, {})
        box.backup.stop()
    end)
end

g.test_recovery_point_persistence = function(cg)
    cg.server = server:new({box_cfg = {checkpoint_count = 2}})
    cg.server:start()
    local prev_vclock, recovery_points = cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        box.backup.start()
        local prev_vclock = box.backup.info().vclock
        box.backup.stop()

        for i = 1, 10 do
            s:insert({i})
            box.backup.recovery_point.create()
        end

        box.snapshot()

        for i = 11, 20 do
            s:insert({i})
            box.backup.recovery_point.create()
        end

        box.backup.start({from_vclock = prev_vclock})
        local recovery_points = box.backup.info().recovery_points
        box.backup.stop()
        return prev_vclock, recovery_points
    end)
    cg.server:restart()
    cg.server:exec(function(prev_vclock, recovery_points)
        box.backup.start({from_vclock = prev_vclock})
        t.assert_equals(box.backup.info().recovery_points, recovery_points)
        box.backup.stop()
    end, {prev_vclock, recovery_points})
end

local g1 = t.group('replication')

g1.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new({})
    local uris = {}
    for i = 1, 3 do
        uris[i] = server.build_listen_uri('replica' .. i, cg.replica_set.id)
    end
    local box_cfg = {
        replication_synchro_quorum = 2,
        election_mode = 'manual',
        replication = uris,
    }
    for i = 1, 3 do
        cg.replica_set:build_and_add_server({
            alias = 'replica' .. i, box_cfg = box_cfg
        })
    end
    cg.replica_set:start()
    local replica1 = cg.replica_set:get_server('replica1')
    cg.replication_synchro_timeout_default = replica1:exec(function()
        return box.cfg.replication_synchro_timeout
    end)
end)

g1.after_all(function(cg)
    cg.replica_set:drop()
end)

g1.before_each(function(cg)
    cg.replica_set:wait_for_fullmesh()
end)

g1.after_each(function(cg)
    local replica1 = cg.replica_set:get_server('replica1')
    replica1:exec(function(timeout)
        box.cfg{replication_synchro_timeout = timeout}
        box.ctl.promote()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end, cg.replication_synchro_timeout)
end)

g1.after_test('test_backup_replication_commited_master', function(cg)
    local replica1 = cg.replica_set:get_server('replica1')
    replica1:exec(function()
        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', false)
        box.backup.stop()
    end)
end)

g1.test_backup_replication_commited_master = function(cg)
    local replica1 = cg.replica_set:get_server('replica1')
    local replica2 = cg.replica_set:get_server('replica2')
    replica1:exec(function()
        box.ctl.promote()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        s:insert({1})
    end)
    -- Otherwise promote may fail.
    replica2:wait_for_vclock_of(replica1)
    replica1:exec(function()
        local fiber = require('fiber')

        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', true)
        box.cfg{replication_synchro_timeout = 1000}
        local f = fiber.new(function()
            local f = fiber.new(function()
                box.backup.start()
            end)
            f:set_joinable(true)
            rawset(_G, 'backup_fiber', f)
            box.space.test:insert({2})
        end)
        f:set_joinable(true)
        rawset(_G, 'dml_fiber', f)
        fiber.yield()
    end)
    replica2:exec(function()
        box.ctl.promote()
    end)
    replica1:exec(function()
        local join_err = function(f)
            local ok, err = f:join()
            if not ok then
                error(err)
            end
            return true
        end
        local f = rawget(_G, 'dml_fiber')
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.SYNC_ROLLBACK,
        }, join_err, f)
        local f = rawget(_G, 'backup_fiber')
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.SYNC_ROLLBACK,
        }, join_err, f)
    end)
end

g1.after_test('test_backup_replication_timeout', function(cg)
    local replica1 = cg.replica_set:get_server('replica1')
    replica1:exec(function()
        local compat = require('compat')
        compat.replication_synchro_timeout = 'default'
        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', false)
        box.backup.stop()
    end)
end)

g1.test_backup_replication_timeout = function(cg)
    local replica1 = cg.replica_set:get_server('replica1')
    replica1:exec(function()
        local fiber = require('fiber')
        local compat = require('compat')

        box.ctl.promote()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        s:insert({1})
        box.cfg{replication_synchro_timeout = 1}
        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', true)
        compat.replication_synchro_timeout = 'new'
        local f = fiber.new(function()
            box.backup.start()
        end)
        f:set_joinable(true)
        fiber.create(function()
            s:insert({2})
        end)
        local ok, err = f:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {type = 'TimedOut'})
    end)
end

g1.after_test('test_backup_replication_commited_replica', function(cg)
    local replica1 = cg.replica_set:get_server('replica1')
    local replica2 = cg.replica_set:get_server('replica2')
    replica1:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    replica2:exec(function()
        box.backup.stop()
    end)
end)

-- Make sure we got committed xlog even is backup is done on replica.
g1.test_backup_replication_commited_replica = function(cg)
    local replica1 = cg.replica_set:get_server('replica1')
    local replica2 = cg.replica_set:get_server('replica2')
    replica1:exec(function()
        local fiber = require('fiber')

        box.ctl.promote()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        s:insert({1})
        fiber.create(function()
            -- Delay writing IPROTO_RAFT_CONFIRM.
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
            s:insert({2})
        end)
        local lsn = box.info.lsn
        -- Wait INSERT is written to WAL.
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.lsn, lsn + 1)
        end)
    end)
    -- Wait INSERT is written to WAL of replica.
    replica2:wait_for_vclock_of(replica1)
    replica2:exec(function()
        t.assert_error_covers({
            type = 'TimedOut',
        }, box.backup.start)
    end)
end

g1.after_test('test_backup_concurrent_stop', function(cg)
    local replica1 = cg.replica_set:get_server('replica1')
    replica1:exec(function()
        local compat = require('compat')
        compat.replication_synchro_timeout = 'default'
        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', false)
        box.backup.stop()
    end)
end)

g1.test_backup_concurrent_stop = function(cg)
    local replica1 = cg.replica_set:get_server('replica1')
    replica1:exec(function()
        local fiber = require('fiber')
        local compat = require('compat')

        box.ctl.promote()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        s:insert({1})
        box.cfg{replication_synchro_timeout = 1}
        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', true)
        compat.replication_synchro_timeout = 'new'
        local f = fiber.new(function()
            fiber.new(function()
                box.backup.stop()
            end)
            box.backup.start()
        end)
        f:set_joinable(true)
        fiber.create(function()
            s:insert({2})
        end)
        local ok, err = f:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {type = 'TimedOut'})
    end)
end

g1.test_recovery_point_replication = function(cg)
    local replica1 = cg.replica_set:get_server('replica1')
    local replica2 = cg.replica_set:get_server('replica2')
    local recovery_points = replica1:exec(function()
        box.ctl.promote()

        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')

        for i = 1, 10 do
            s:insert({i})
            box.backup.recovery_point.create()
        end

        box.backup.start()
        local recovery_points = box.backup.info().recovery_points
        box.backup.stop()
        return recovery_points
    end)
    replica2:wait_for_vclock_of(replica1)
    replica2:exec(function(recovery_points)
        box.backup.start()
        t.assert_equals(box.backup.info().recovery_points, recovery_points)
        box.backup.stop()
    end, {recovery_points})
end

-- No server restart between tests.
local g2 = t.group('fast')

g2.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local tweaks = require('internal.tweaks')
        rawset(_G, 'box_recovery_point_count_max',
               tweaks.box_recovery_point_count_max)
        box.schema.user.create('stranger')
    end)
end)

g2.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

g2.after_each(function(cg)
    cg.server:exec(function()
        local tweaks = require('internal.tweaks')

        box.backup.stop()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        -- Truncation of a system space is not possible.
        for _, tuple in box.space._recovery_point:pairs() do
            box.space._recovery_point:delete(
                {tuple.timestamp, tuple.replica_id, tuple.lsn})
        end
        tweaks.box_recovery_point_count_max =
                rawget(_G, 'box_recovery_point_count_max')
    end)
end)

g2.test_recovery_point_create_args_checks = function(cg)
    cg.server:exec(function()
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'expected table as 1 argument',
        }, box.backup.recovery_point.create, 1)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'label option should be string',
        }, box.backup.recovery_point.create, {label = {}})
    end)
end

g2.test_recovery_point_create_result = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.create_space('test')
        s:create_index('pk')

        for i = 1, 10 do
            s:insert({i})
            local p = box.backup.recovery_point.create()
            t.assert_almost_equals(p.timestamp, fiber.time(), 1)
            p.timestamp = nil
            t.assert_equals(p, {
                replica_id = box.info.id,
                -- recovery point lsn is one less due to UPDATE.
                lsn = box.info.lsn - 1,
            })
        end

        for i = 11, 20 do
            local p = box.backup.recovery_point.create({label = i})
            t.assert_equals(p.label, tostring(i))
        end
    end)
end

-- Test it is possible to create recovery points when there were no writes.
g2.test_recovery_point_create_no_changes = function(cg)
    cg.server:exec(function()
        local p1 = box.backup.recovery_point.create()
        local p2 = box.backup.recovery_point.create()
        t.assert_gt(p2.lsn, p1.lsn)
    end)
end

g2.test_recovery_point_create_same_timestamp = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        -- We can make same timestamp for sure with recovery point API.
        local timestamp = fiber.time()
        box.space._recovery_point:insert({timestamp, 1, 1, {label = '1'}})
        box.space._recovery_point:insert({timestamp, 1, 2, {label = '2'}})
    end)
end

g2.test_recovery_point_list_labels = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        local expected = {}
        for i = 1, 10 do
            s:insert({i})
            table.insert(expected,
                         box.backup.recovery_point.create({label = i}))
        end

        box.backup.start()
        t.assert_equals(box.backup.info().recovery_points, expected)
        box.backup.stop()
    end)
end

g2.test_recovery_point_list_filtering_incremental = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        for i = 1, 10 do
            s:insert({i})
            box.backup.recovery_point.create()
        end

        box.backup.start()
        local prev_vclock = box.backup.info().vclock
        box.backup.stop()

        local expected = {}
        for i = 11, 20 do
            s:insert({i})
            table.insert(expected, box.backup.recovery_point.create())
        end

        box.backup.start({from_vclock = prev_vclock})
        for i = 21, 30 do
            s:insert({i})
            box.backup.recovery_point.create()
        end
        t.assert_equals(box.backup.info().recovery_points, expected)
        box.backup.stop()
    end)
end

g2.test_recovery_point_list_filtering_full = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        for i = 1, 10 do
            s:insert({i})
            box.backup.recovery_point.create()
        end
        box.snapshot()

        local expected = {}
        for i = 11, 20 do
            s:insert({i})
            table.insert(expected, box.backup.recovery_point.create())
        end

        box.backup.start()
        for i = 21, 30 do
            s:insert({i})
            box.backup.recovery_point.create()
        end
        t.assert_equals(box.backup.info().recovery_points, expected)
        box.backup.stop()
    end)
end

g2.test_recovery_point_limit = function(cg)
    cg.server:exec(function()
        local tweaks = require('internal.tweaks')

        local s = box.schema.create_space('test')
        s:create_index('pk')

        tweaks.box_recovery_point_count_max = 10
        local expected = {}
        for i = 1, 12 do
            s:insert({i})
            table.insert(expected, box.backup.recovery_point.create())
        end
        table.remove(expected, 1)
        table.remove(expected, 1)

        box.backup.start()
        t.assert_equals(box.backup.info().recovery_points, expected)
        box.backup.stop()
    end)
end

g2.after_test('test_recovery_point_concurrent_create', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end)

-- Test we don't purge incomplete point being created.
g2.test_recovery_point_concurrent_create = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local tweaks = require('internal.tweaks')

        local s = box.schema.create_space('test')
        s:create_index('pk')

        tweaks.box_recovery_point_count_max = 10
        local expected = {}
        for i = 1, 9 do
            s:insert({i})
            table.insert(expected, box.backup.recovery_point.create())
        end

        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f1 = fiber.new(function()
            table.insert(expected, box.backup.recovery_point.create())
        end)
        f1:set_joinable(true)
        fiber.yield()
        local f2 = fiber.new(function()
            table.insert(expected, box.backup.recovery_point.create())
        end)
        f2:set_joinable(true)
        fiber.yield()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert(f1:join())
        t.assert(f2:join())
        table.remove(expected, 1)
        box.backup.start()
        t.assert_equals(box.backup.info().recovery_points, expected)
        box.backup.stop()
    end)
end

g2.test_recovery_point_disable_from_txn = function(cg)
    cg.server:exec(function()
        box.begin()
        t.assert_error_covers({
            type = 'ClientError',
            name = 'ACTIVE_TRANSACTION',
        }, box.backup.recovery_point.create)
        box.rollback()
    end)
end

g2.after_test('test_recovery_point_check_txn_last_row', function(cg)
    cg.server:exec(function()
        local trigger = require('trigger')

        trigger.del('box.before_commit', 'test')
    end)
end)

g2.test_recovery_point_check_txn_last_row = function(cg)
    cg.server:exec(function()
        local trigger = require('trigger')

        local s = box.schema.create_space('test', {is_local = true})
        s:create_index('pk')

        local i = 1
        trigger.set('box.before_commit', 'test', function()
            s:insert({i})
            i = i + 1
        end)

        t.assert_error_covers({
            type = 'ClientError',
            name = 'RECOVERY_POINT_TXN_LAST_ROW',
        }, box.backup.recovery_point.create)
    end)
end

-- To cover case in on commit trigger when statement in txn has no row.
g2.test_recovery_point_temporary_space_in_txn = function(cg)
    cg.server:exec(function()
        local trigger = require('trigger')

        local s = box.schema.create_space('test1', {temporary = true})
        s:create_index('pk')

        local i = 1
        trigger.set('box.before_commit', 'test', function()
            s:insert({i})
            i = i + 1
        end)

        box.backup.recovery_point.create()
    end)
end

g2.test_recovery_point_list_yields = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.create_space('test')
        s:create_index('pk')

        for i = 1, 10 do
            s:insert({i})
            box.backup.recovery_point.create()
        end

        box.internal.recovery_point_yield_loops = 10
        box.backup.start()
        local csw = fiber.self():csw()
        box.backup.info()
        t.assert_gt(fiber.self():csw(), csw)
        box.backup.stop()
    end)
end

--
-- Test case when _recovery_point has tuples with extra fields inserted
-- manually.
--
g2.test_recovery_point_extra_fields = function(cg)
    cg.server:exec(function()
        local tweaks = require('internal.tweaks')

        tweaks.box_recovery_point_count_max = 10
        box.space._recovery_point:insert(
                                {1.1, 1, 1, {label = '1'},'something'})
        for _ = 1, 20 do
             box.backup.recovery_point.create()
        end
        box.backup.start()
        t.assert_equals(#box.backup.info().recovery_points, 10)
        box.backup.stop()
    end)
end

g2.test_recovery_point_suid = function(cg)
    cg.server:exec(function()
        box.session.su('stranger', box.backup.recovery_point.create)
    end)
end

g2.test_backup_info_suid = function(cg)
    cg.server:exec(function()
        box.backup.start()
        box.session.su('stranger', box.backup.info)
        box.backup.stop()
    end)
end
