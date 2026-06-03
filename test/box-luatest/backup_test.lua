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

local backup = function(cg, files)
    if cg.backup_dir ~= nil then
        fio.rmtree(cg.backup_dir)
    end
    cg.backup_dir = fio.tempdir()
    for _, file in ipairs(files) do
        local path_src = fio.pathjoin(cg.server.workdir, file)
        local path_dst = fio.pathjoin(cg.backup_dir, file)
        fio.copyfile(path_src, path_dst)
    end
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
        })
        box.backup.stop()

        -- Case when backup is not based on latest snapshot.
        s:insert({21})
        box.snapshot()
        local files = box.backup.start(1)
        t.assert_equals(box.backup.info(), {
            files = files,
            vclock = vclock,
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
        election_timeout = 1000,
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
        local fiber = require('fiber')

        box.ctl.promote()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        s:insert({1})
        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', true)
        box.cfg{replication_synchro_timeout = 1000}
        local f = fiber.new(function()
            local f = fiber.new(function()
                box.backup.start()
            end)
            f:set_joinable(true)
            rawset(_G, 'backup_fiber', f)
            s:insert({2})
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
