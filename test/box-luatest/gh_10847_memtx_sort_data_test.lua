local server = require('luatest.server')
local t = require('luatest')

local g_sk_types = t.group('memtx_sort_data_test-sk-types', {
    {pk_type = 'TREE', sk = {hint = true}},
    {pk_type = 'TREE', sk = {hint = false}},
    {pk_type = 'TREE', sk = {path = 'array[*]'}},
    {pk_type = 'TREE', sk = {is_nullable = true}},
    {pk_type = 'TREE', sk = {is_nullable = true,
                             exclude_null = true}},
    {pk_type = 'HASH', sk = {}},
})
local g_generic = t.group('memtx_sort_data_test-generic')
local g_invalid_file = t.group('memtx_sort_data_test-invalid_file')
local g_mvcc = t.group('memtx_sort_data_test-mvcc',
                       {{pk_type = 'TREE'}, {pk_type = 'HASH'}})
local g_graceful_shutdown = t.group('memtx_sort_data_test-snapshot')


g_sk_types.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {memtx_use_sort_data = true,
                   checkpoint_count  = 3},
        alias = 'master'
    })
    cg.server:start()
end)

g_sk_types.after_all(function(cg)
    cg.server:stop()
end)

g_sk_types.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Test that the SK sort using the sort data is correct.
g_sk_types.test_memtx_sort_data = function(cg)
    -- Create the space and indexes and write snapshot with sort data.
    cg.server:exec(function(pk_type, sk)
        local s = box.schema.create_space('test')
        s:create_index('pk', {type = pk_type, parts = {1, 'unsigned'}})
        s:create_index('sk', {type = 'tree', hint = sk.hint, unique = false,
                              parts = {2, 'unsigned', path = sk.path,
                                       is_nullable = sk.is_nullable,
                                       exclude_null = sk.exclude_null}})

        local function sk_object()
            local max = 1000000
            if sk.path ~= nil then
                local count = math.random(1, 3)
                local array = {}
                for i = 1, count do -- luacheck: no unused
                    table.insert(array, math.random(1, max))
                end
                return {array = array, scalar = math.random(1, max)}
            else
                -- 1/3 of nulls.
                if sk.is_nullable and math.random(1, 3) == 1 then
                    return box.NULL
                else
                    return math.random(1, max)
                end
            end
        end

        for i = 1, 100 do
            s:insert({i, sk_object()})
        end

        box.snapshot()
    end, {cg.params.pk_type, cg.params.sk})

    -- Start using the sort data.
    cg.server:restart()
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'sk\' of space \'test\''))

    -- Check the order of tuples in SK.
    cg.server:exec(function(sk_is_mk, nulls_excluded)
        local fiber = require('fiber')
        local s = box.space.test
        t.assert_equals(s.index.pk:len(), 100)

        if sk_is_mk then
            t.assert_ge(s.index.sk:len(), 100)
            local processed = 0
            for _, tuple in s.index.sk:pairs() do
                -- Can't verify tuple order, only access
                -- its fields and verify to have no crash.
                t.assert(tuple[2].array ~= nil)
                processed = processed + 1
                if processed % 100 == 0 then
                    fiber.yield()
                end
            end
            t.assert_equals(processed, s.index.sk:len())
            return
        end

        if not nulls_excluded then
            t.assert_equals(s.index.sk:len(), 100)
        else
            t.assert_lt(s.index.sk:len(), 100)
        end
        local key_def = require('key_def')
        local kd = key_def.new(s.index.sk.parts)
        local prev = nil
        local processed = 0
        for _, tuple in s.index.sk:pairs() do
            if processed > 0 then
                t.assert_ge(kd:compare(tuple, prev), 0)
            end
            prev = tuple
            processed = processed + 1
        end
        t.assert_equals(processed, s.index.sk:len())
        fiber.yield()
    end, {cg.params.sk.path ~= nil and string.find(cg.params.sk.path, '*'),
          cg.params.sk.exclude_null})
end

-- Create a space with data, primary and secondary key.
local function create_test_space(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        -- The SK tuple order is dirrefent from PK.
        s:create_index('sk', {parts = {1, 'unsigned', sort_order = 'desc'}})
        for i = 1, 100 do
            s:insert({i})
        end
    end)
end

g_generic.before_each(function(cg)
    -- Run the server with memtx sort data enabled.
    cg.server = server:new({
        box_cfg = {memtx_use_sort_data = true},
        alias = 'master'
    })
    cg.server:start()
end)

g_generic.after_each(function(cg)
    -- Drop the server.
    cg.server:drop()
end)

-- Test that functional indexes aren't affected.
g_generic.test_func_index = function(cg)
    create_test_space(cg)

    -- Create a functional key and a snapshot with sort data file.
    cg.server:exec(function()
        local s = box.space.test
        local func_lua_code = 'function(tuple) return {tuple[1]} end'
        box.schema.func.create('func', {body = func_lua_code,
                                        is_deterministic = true,
                                        is_sandboxed = true})
        s:create_index('fk', {type = 'tree', unique = false, func = 'func',
                              parts = {1, 'unsigned', sort_order = 'desc'}})
        box.snapshot()
    end)

    -- Start using the sort data file.
    cg.server:restart()

    -- Must use the sort data on a regular SK.
    t.assert(cg.server:grep_log('Using MemTX sort data for building' ..
                                ' index \'sk\' of space \'test\'') ~= nil)

    -- Must NOT use the sort data on a functional SK.
    t.assert(cg.server:grep_log('Using MemTX sort data for building' ..
                                ' index \'fk\' of space \'test\'') == nil)

    -- Check the functional key order (which is descending).
    cg.server:exec(function()
        local fiber = require('fiber')
        local i = 100
        for _, tuple in box.space.test.index.fk:pairs() do
            t.assert_equals(tuple[1], i)
            i = i - 1
        end
        t.assert_equals(i, 0)
        fiber.yield()
    end)
end

-- Test that local spaces are handled as expected.
g_generic.test_local_space = function(cg)
    -- Create a local space with data and primary and secondary keys.
    cg.server:exec(function()
        box.space._space:alter({}) -- No-op to update the VClock.
        local s = box.schema.create_space('test', {is_local = true})
        s:create_index('pk')
        s:create_index('sk', {parts = {1, 'unsigned', sort_order = 'desc'}})
        for i = 1, 100 do
            s:insert({i})
        end
        box.space._space:alter({}) -- No-op to update the VClock.
        box.snapshot()
    end)

    -- Start using the sort data file.
    cg.server:restart()

    -- Must use the sort data on the SK.
    t.assert(cg.server:grep_log('Using MemTX sort data for building' ..
                                ' index \'sk\' of space \'test\'') ~= nil)

    -- Check the SK order (which is descending).
    cg.server:exec(function()
        local fiber = require('fiber')
        local i = 100
        for _, tuple in box.space.test.index.sk:pairs() do
            t.assert_equals(tuple[1], i)
            i = i - 1
        end
        t.assert_equals(i, 0)
        fiber.yield()
    end)
end

-- Test that empty sort data for an index is handled appropriately.
g_generic.test_empty_sort_data = function(cg)
    -- Create a space with data and indexes.
    cg.server:exec(function()
        local s = box.schema.create_space('test', {is_local = true})
        s:create_index('pk')
        s:create_index('sk1')
        s:create_index('sk2', {parts = {2, 'unsigned', is_nullable = true,
                                        exclude_null = true}})
        s:create_index('sk3', {parts = {1, 'unsigned', sort_order = 'desc'}})
        for i = 1, 100 do
            s:insert({i, box.NULL})
        end
        box.snapshot()
    end)

    -- Start using the sort data file.
    cg.server:restart()

    -- Must use the sort data on secondary keys.
    t.assert(cg.server:grep_log('Using MemTX sort data for building' ..
                                ' index \'sk1\' of space \'test\'') ~= nil)
    t.assert(cg.server:grep_log('Using MemTX sort data for building' ..
                                ' index \'sk2\' of space \'test\'') ~= nil)
    t.assert(cg.server:grep_log('Using MemTX sort data for building' ..
                                ' index \'sk3\' of space \'test\'') ~= nil)

    -- Check SK orders.
    cg.server:exec(function()
        local fiber = require('fiber')
        local i = 100
        t.assert_equals(box.space.test.index.sk3:len(), box.space.test:count())
        for _, tuple in box.space.test.index.sk3:pairs() do
            t.assert_equals(tuple[1], i)
            i = i - 1
        end
        fiber.yield()
        t.assert_equals(i, 0)
        t.assert_equals(box.space.test.index.sk1:len(), box.space.test:count())
        for _, tuple in box.space.test.index.sk1:pairs() do
            i = i + 1
            t.assert_equals(tuple[1], i)
        end
        fiber.yield()
        t.assert_equals(i, 100)
        t.assert_equals(box.space.test.index.sk2:len(), 0)
    end)
end

-- Test that a before_replace trigger on the test space prevent using sort data.
g_generic.test_before_replace = function(cg)
    create_test_space(cg)

    -- Create a snapshot and sort data file.
    cg.server:exec(function()
        box.snapshot()
    end)

    -- Load it with the _index.before_recovery_replace trigger set up.
    local register_before_recovery_replace = [[
        local trigger = require('trigger')
        trigger.set('box.space.test.before_recovery_replace', 'test_trigger',
                    function(old, new, space, req, header, body)
                        return new
                    end)
    ]]
    cg.server:restart({
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = register_before_recovery_replace,
        }
    })

    -- Must not use the sort data because of the trigger.
    local msg = 'Using MemTX sort data for building' ..
                ' index \'sk\' of space \'test\''
    t.assert(cg.server:grep_log(msg) == nil)

    -- Now load it with a regular test.before_replace trigger set up.
    local register_before_replace = [[
        local trigger = require('trigger')
        trigger.set('box.space.test.before_replace', 'test_trigger',
                    function(old, new, space, req, header, body)
                        return new
                    end)
    ]]
    cg.server:restart({
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = register_before_replace,
        }
    })

    -- Now it should use the sort data file (the regular before_replace trigger
    -- does not fire on recovery so it should not affect the ability to use it).
    t.assert(cg.server:grep_log(msg) ~= nil)
end

-- Test that a before_replace trigger on _index space prevent using sort data.
g_generic.test_index_before_replace = function(cg)
    create_test_space(cg)

    -- Create a snapshot and sort data file.
    cg.server:exec(function()
        box.snapshot()
    end)

    -- Load it with the _index.before_recovery_replace trigger set up.
    local register_before_recovery_replace = [[
        local trigger = require('trigger')
        trigger.set('box.space._index.before_recovery_replace', 'test_trigger',
                    function(old, new, space, req, header, body)
                        return new
                    end)
    ]]
    cg.server:restart({
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = register_before_recovery_replace,
        }
    })

    -- Must not use the sort data because of the trigger.
    local msg = 'memtx_use_sort_data = true but no memtx sort data used:' ..
                ' the _index space has before_replace triggers'
    t.assert(cg.server:grep_log(msg))

    -- Now load it with a regular _index.before_replace trigger set up.
    local register_before_replace = [[
        local trigger = require('trigger')
        trigger.set('box.space._index.before_replace', 'test_trigger',
                    function(old, new, space, req, header, body)
                        return new
                    end)
    ]]
    cg.server:restart({
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = register_before_replace,
        }
    })

    -- Now it should use the sort data file (the regular before_replace trigger
    -- does not fire on recovery so it should not affect the ability to use it).
    t.assert(cg.server:grep_log('using the memtx sort data from'))
end

-- Test that the sort data works for multiple spaces.
g_generic.test_multiple_spaces = function(cg)
    create_test_space(cg)

    -- Create the second space with data, save a snapshot and load it.
    cg.server:exec(function()
        local s = box.schema.create_space('test2')
        s:create_index('pk')
        s:create_index('sk')
        for i = 1, 100 do
            s:insert({i})
        end
        box.snapshot()
    end)
    cg.server:restart()

    -- We must both spaces loaded using the sort data.
    local log_format = 'Using MemTX sort data for building ' ..
                       'index \'sk\' of space \'%s\''
    local log_test = string.format(log_format, 'test')
    local log_test2 = string.format(log_format, 'test2')
    t.assert(cg.server:grep_log(log_test))
    t.assert(cg.server:grep_log(log_test2))
end

-- Test that existence of vinyl spaces does not break the recovery.
g_generic.test_vinyl_space_ignored = function(cg)
    create_test_space(cg)

    -- Create a vinyl space with data, save a snapshot and load it.
    cg.server:exec(function()
        local s = box.schema.create_space('test_vinyl', {engine = 'vinyl'})
        s:create_index('pk')
        s:create_index('sk')
        for i = 1, 100 do
            s:insert({i})
        end
        box.snapshot()
    end)
    cg.server:restart()

    -- We must have memtx space loaded using the sort data.
    local log_format = 'Using MemTX sort data for building ' ..
                       'index \'sk\' of space \'%s\''
    local log_memtx = string.format(log_format, 'test')
    local log_vinyl = string.format(log_format, 'test_vinyl')
    t.assert(cg.server:grep_log(log_memtx) ~= nil)
    t.assert(cg.server:grep_log(log_vinyl) == nil)
end

-- Test recovery using both snapshot and WAL.
g_generic.test_recover_with_wal = function(cg)
    create_test_space(cg)

    cg.server:exec(function()
        -- Create a snapshot and sort data file.
        box.snapshot()

        -- Perform more operations to create a WAL log.
        for i = 1, 100 do
            box.space.test:insert({1000 + i})
        end
    end)

    -- Recover from the snapshot and WAL.
    cg.server:restart()
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'sk\' of space \'test\''))
    t.assert(cg.server:grep_log('entering the event loop'))
end

-- Test recovery without sort data file if memtx_use_sort_data = true.
g_generic.test_recover_without_sort_data = function(cg)
    create_test_space(cg)

    cg.server:exec(function()
        -- Create a snapshot and sort data file.
        box.snapshot()

        -- Remove all sort data files.
        local fio = require('fio')
        local glob = fio.pathjoin(box.cfg.memtx_dir, '*.sortdata')
        local files = fio.glob(glob)
        t.assert(#files > 0) -- a sort data file must have been created
        for _, file in pairs(files) do
            fio.unlink(file)
        end
    end)

    -- Recover without a sort data file using the regular SK build.
    cg.server:restart()
    t.assert(cg.server:grep_log('memtx_use_sort_data = true but' ..
                                ' no memtx sort data file found'))
    t.assert(cg.server:grep_log('Space \'test\': done'))
    t.assert(cg.server:grep_log('entering the event loop'))
end

-- Test that the sort data file is not recreated on box.snapshot after removal.
g_generic.test_remove_sortdata_keep_snap = function(cg)
    create_test_space(cg)

    cg.server:exec(function()
        -- Create a snapshot and sort data file.
        box.snapshot()

        -- Remove all sort data files.
        local fio = require('fio')
        local glob = fio.pathjoin(box.cfg.memtx_dir, '*.sortdata')
        local files = fio.glob(glob)
        t.assert(#files > 0) -- a sort data file must have been created
        for _, file in pairs(files) do
            fio.unlink(file)
        end

        -- Call box.snapshot() once again: sort data file not recreated.
        box.snapshot()
        t.assert_equals(#fio.glob(glob), 0)
    end)
end

-- Test that checkpoint fails if a sort data file exists already.
g_generic.test_remove_snap_keep_sortdata = function(cg)
    create_test_space(cg)

    cg.server:exec(function()
        -- Remove all snapshots and sort data files.
        local fio = require('fio')
        local glob_snap = fio.pathjoin(box.cfg.memtx_dir, '*.snap')
        local glob_sortdata = fio.pathjoin(box.cfg.memtx_dir, '*.sortdata')
        local snapshots = fio.glob(glob_snap)
        local sort_data_files = fio.glob(glob_sortdata)
        t.assert(#snapshots > 0)
        t.assert(#sort_data_files > 0)
        for _, file in pairs(snapshots) do
            fio.unlink(file)
        end
        for _, file in pairs(sort_data_files) do
            fio.unlink(file)
        end

        -- Create a snapshot and sort data file.
        box.snapshot()
        t.assert(#fio.glob(glob_snap) == 1)
        t.assert(#fio.glob(glob_sortdata) == 1)

        -- Remove the .snap file.
        fio.unlink(fio.glob(glob_snap)[1])
        t.assert(#fio.glob(glob_snap) == 0)

        -- Call to box.snapshot() fails: the sort data file exists already.
        t.assert_error_msg_contains('.sortdata\': File exists', box.snapshot)
        t.assert(#fio.glob(glob_snap) == 0)
        t.assert(#fio.glob(glob_sortdata) == 1)
    end)
end

-- Test that both snapshot and sort data file recreated if are absent.
g_generic.test_remove_snap_and_sortdata = function(cg)
    create_test_space(cg)

    cg.server:exec(function()
        -- Create a snapshot and sort data file.
        box.snapshot()

        -- Remove all snapshots and sort data files.
        local fio = require('fio')
        local glob_snap = fio.pathjoin(box.cfg.memtx_dir, '*.snap')
        local glob_sortdata = fio.pathjoin(box.cfg.memtx_dir, '*.sortdata')
        local snapshots = fio.glob(glob_snap)
        local sort_data_files = fio.glob(glob_sortdata)
        t.assert(#snapshots > 0)
        t.assert(#sort_data_files > 0)
        for _, file in pairs(snapshots) do
            fio.unlink(file)
        end
        for _, file in pairs(sort_data_files) do
            fio.unlink(file)
        end

        -- Call box.snapshot() once again: both files recreated.
        box.snapshot()
        t.assert_equals(#fio.glob(glob_snap), 1)
        t.assert_equals(#fio.glob(glob_sortdata), 1)
    end)
end

-- Test that .snap file creation failure is handled correctly.
g_generic.test_snap_create_error = function(cg)
    create_test_space(cg)

    cg.server:exec(function()
        -- One way to fail the .snap file creation is to create a
        -- filesystem entry with the name of an in-progress snapshot.
        local fio = require('fio')
        fio.mkdir(fio.pathjoin(box.cfg.memtx_dir,
                               string.format('%020d.snap.inprogress',
                                             box.info.signature)))

        -- The checkpoint is aborted successfully.
        local msg = '.snap.inprogress\': File exists'
        t.assert_error_msg_contains(msg, box.snapshot)
    end)
end

-- Test that checkpoint abortion discards the written .sortdata file.
g_generic.test_snapshot_abort_discards_sortdata_file = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fio = require('fio')
        local fiber = require('fiber')
        local function wait_for_file(filename)
            t.helpers.retrying({}, function()
                t.assert(fio.path.exists(filename))
            end)
        end

        -- Remove all sort data files.
        local glob = fio.pathjoin(box.cfg.memtx_dir, '*.sortdata')
        local files = fio.glob(glob)
        t.assert(#files > 0) -- a sort data file must have been created
        for _, file in pairs(files) do
            fio.unlink(file)
        end

        -- Unsuccessfully attempt to write a snapshot: no sort data file kept.
        -- Can't do it in the current fiber, so let's create another one, make
        -- it start checkpoint and kill it.
        local f1 = fiber.new(box.snapshot)
        f1:set_joinable(true)
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', true)

        -- Wait for the inprogress sort data file to appear.
        wait_for_file(string.format('%020d.sortdata.inprogress',
                                    box.info.signature))

        -- Checkpoint started, the file must be there.
        t.assert(box.info.gc().checkpoint_is_in_progress)
        local glob_inprogress =
            fio.pathjoin(box.cfg.memtx_dir, '*.sortdata.inprogress')
        t.assert_equals(#fio.glob(glob_inprogress), 1)
        t.assert_equals(#fio.glob(glob), 0)

        -- Kill the checkpoint fiber and see the inprogress file removed.
        f1:cancel()
        t.assert(not f1:join())
        t.assert_equals(#fio.glob(glob_inprogress), 0)
        t.assert_equals(#fio.glob(glob), 0)

        -- Now do it successfully.
        local f2 = fiber.new(box.snapshot)
        f2:set_joinable(true)
        wait_for_file(string.format('%020d.sortdata.inprogress',
                                    box.info.signature))

        -- Checkpoint started, the sort data file must be there.
        t.assert(box.info.gc().checkpoint_is_in_progress)
        t.assert_equals(#fio.glob(glob_inprogress), 1)
        t.assert_equals(#fio.glob(glob), 0)

        -- Let checkpoint fiber finish and see the sort data materialized.
        box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', false)
        t.assert(f2:join())
        t.assert_equals(#fio.glob(glob_inprogress), 0)
        t.assert_equals(#fio.glob(glob), 1)
    end)
end

-- Test that the .sortdata files are garbage-collected.
g_generic.test_gc = function(cg)
    create_test_space(cg)

    cg.server:exec(function()
        local fio = require('fio')
        local glob_snap = fio.pathjoin(box.cfg.memtx_dir, '*.snap')
        local glob_sortdata = fio.pathjoin(box.cfg.memtx_dir, '*.sortdata')

        local prev_snapshots = nil
        local function check()
            local snapshots = fio.glob(glob_snap)
            local sort_data_files = fio.glob(glob_sortdata)

            t.assert_equals(#sort_data_files, #snapshots)
            if #snapshots < box.cfg.checkpoint_count then
                if prev_snapshots ~= nil then
                    t.assert_equals(#snapshots, #prev_snapshots + 1)
                end
            else
                t.assert_equals(#snapshots, box.cfg.checkpoint_count)
            end
            prev_snapshots = snapshots
        end

        -- Create checkpoints and verify the .sortdata file count is valid.
        for i = 1, box.cfg.checkpoint_count * 2 do -- luacheck: no unused
            box.space._space:alter({}) -- No-op to update the VClock.
            box.snapshot()
            check()
        end
    end)
end

-- Test that box.backup.* work as expected.
g_generic.test_box_backup = function(cg)
    create_test_space(cg)

    cg.server:exec(function()
        local fio = require('fio')
        local glob_snap = fio.pathjoin(box.cfg.memtx_dir, '*.snap')
        local glob_sortdata = fio.pathjoin(box.cfg.memtx_dir, '*.sortdata')
        local fmt_snap = '%020d.snap'
        local fmt_sortdata = '%020d.sortdata'
        local vclock_had_sortdata = {}

        -- Check the files available for backup.
        local function check(backup, expected_snap_count,
                             expected_sortdata_count)
            assert(backup[1]:match('.snap$'))
            local snap_count = 0
            local sortdata_count = 0
            local vclock_base = tonumber(fio.basename(backup[1], '.snap'))
            local vclock_last = box.info.vclock[1]
            for vclock = vclock_base, vclock_last do
                t.assert(fio.path.exists(string.format(fmt_snap, vclock)))
                snap_count = snap_count + 1
                if vclock_had_sortdata[vclock] then
                    t.assert(fio.path.exists(string.format(fmt_sortdata,
                                                           vclock)))
                    sortdata_count = sortdata_count + 1
                end
            end

            -- Check and show the caller position if it fails.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline
            local msg = file .. ':' .. line
            t.assert_equals(snap_count, expected_snap_count, msg)
            t.assert_equals(sortdata_count, expected_sortdata_count, msg)
        end

        -- Create a snapshot and remember if it had sort data included.
        local function snapshot(use_sort_data)
            box.space._space:alter({}) -- No-op to update the VClock.
            vclock_had_sortdata[box.info.vclock[1]] = use_sort_data
            box.cfg{memtx_use_sort_data = use_sort_data}
            box.snapshot()
        end

        -- Create a snapshot and sort data file.
        snapshot(true)
        local backup = box.backup.start()
        check(backup, 1, 1)
        box.backup.stop()

        -- Create a snapshot without sort data file.
        snapshot(false)
        backup = box.backup.start()
        check(backup, 1, 0)
        box.backup.stop()

        -- Create a snapshot with sort data again.
        snapshot(true)
        backup = box.backup.start()
        check(backup, 1, 1)
        box.backup.stop()

        -- Supposed usecase.
        box.cfg{checkpoint_count = 5}
        snapshot(true)
        snapshot(false)
        snapshot(true)
        snapshot(false)
        snapshot(true)
        backup = box.backup.start(4)
        snapshot(false)
        snapshot(false)
        snapshot(false)
        snapshot(false)
        snapshot(false)
        check(backup, 10, 3) -- Would be 5:0 if not working.
        box.cfg{checkpoint_count = 1}
        snapshot(true)
        check(backup, 11, 4) -- Would be 1:1 if not working.
        box.backup.stop()

        -- Check the garbage is removed after next GC run.
        snapshot(false)
        t.assert_equals(#fio.glob(glob_snap), 1)
        t.assert_equals(#fio.glob(glob_sortdata), 0)
        t.assert(fio.path.exists(string.format(fmt_snap, box.info.vclock[1])))
    end)
end

g_invalid_file.before_each(function(cg)
    -- Run the server with memtx sort data enabled.
    cg.server = server:new({
        box_cfg = {memtx_use_sort_data = true},
        alias = 'master'
    })
    cg.server:start()
end)

g_invalid_file.after_each(function(cg)
    -- Drop the server.
    cg.server:drop()
end)

-- Test that an invalid sort data file is handled correctly.
g_invalid_file.test_invalid_file = function(cg)
    local function recreate_sort_data_file(file)
        -- Drop the last sort data file created.
        local fio = require('fio')
        local glob = fio.pathjoin(box.cfg.memtx_dir, '*.sortdata')
        local files = fio.glob(glob)
        t.assert(#files > 0)
        table.sort(files)
        local last_sort_data = files[#files]
        fio.unlink(last_sort_data)

        -- First create the file header.
        local header = ''
        header = header .. ((file.file_magic or 'SORTDATA') .. '\n')
        header = header .. ((file.file_version or '1') .. '\n')
        header = header .. ((file.version or
                ('Version: ' .. box.info.version)) .. '\n')
        header = header .. ((file.instance or
                ('Instance: ' .. box.info.uuid)) .. '\n')
        header = header .. ((file.vclock or
                ('VClock: {1: ' .. box.info.vclock[1] .. '}')) .. '\n')
        header = header .. '\n'
        local header_size = #header

        -- Then create the entry list.
        local entries_fmt = (file.entries or 'Entries: 0000000002') .. '\n' ..
                            '%010d/%010u: %016x, %016x, %20d\n' ..
                            '%010d/%010u: %016x, %016x, %20d\n' ..
                            '\n'
        local entries_size = #string.format(entries_fmt, 0, 0, 0, 0, 0,
                                                         0, 0, 0, 0, 0)
        local pk_data = file.pk_ptrs or '0000000100000002' -- PK tuple pointers.
        local sk_data = file.sk_ptrs or '0000000100000002' -- The same order.
        local pk_data_size = #pk_data
        local sk_data_size = #sk_data
        local pk_data_offset = header_size + entries_size
        local sk_data_offset = pk_data_offset + pk_data_size
        local entries = string.format(entries_fmt,
                                      512, 0, pk_data_offset, pk_data_size, 2,
                                      512, 1, sk_data_offset, sk_data_size, 2)
        assert(#entries == entries_size)

        -- Create the file.
        local f = fio.open(last_sort_data, {'O_WRONLY', 'O_CREAT'})
        f:write(header)
        f:write(entries)
        f:write(pk_data)
        f:write(sk_data)
        f:close()
    end

    local function expect_success()
        cg.server:restart({})
        t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                    'index \'sk\' of space \'test\''))
        t.assert(cg.server:grep_log('entering the event loop'))
    end

    local function expect_fail(msg)
        -- Recover using the new sort data file.
        cg.server:restart({})
        t.assert(cg.server:grep_log(msg))
        t.assert(cg.server:grep_log('Adding 2 keys to TREE index \'sk\' ...'))
        t.assert(cg.server:grep_log('entering the event loop'))
    end

    local function expect_panic(msg)
        -- Recover using the new invalid data file.
        cg.server:restart({}, {wait_until_ready = false})
        local fio = require('fio')
        t.helpers.retrying({}, function()
            local filename = fio.pathjoin(cg.server.workdir,
                                          cg.server.alias .. '.log')
            t.assert(cg.server:grep_log(msg, nil, {filename = filename}))
        end)

        -- Drop the invalid sort data, restart appropriately and
        -- create a new snapshot (with a valid sort data file).
        local glob = fio.pathjoin(cg.server.workdir, '*.sortdata')
        local files = fio.glob(glob)
        t.assert(#files > 0)
        table.sort(files)
        fio.unlink(files[#files])
        cg.server:restart({})
        cg.server:exec(function()
            box.space._space:alter({}) -- No-op to update the VClock.
            box.snapshot()
        end)
    end

    -- Create a space with data and a snapshot.
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:create_index('sk', {hint = false})
        s:insert({1})
        s:insert({2})
        box.snapshot()
    end)

    cg.server:exec(recreate_sort_data_file, {{file_magic = 'Invalid'}})
    expect_fail('file header read failed')

    cg.server:exec(recreate_sort_data_file, {{file_version = 'Invalid'}})
    expect_fail('file header read failed')

    cg.server:exec(recreate_sort_data_file, {{version = 'Invalid version key'}})
    expect_fail('file header read failed')

    cg.server:exec(recreate_sort_data_file, {{instance = 'Invalid uuid key'}})
    expect_fail('file header read failed')

    cg.server:exec(recreate_sort_data_file, {{instance = 'Instance: invalid'}})
    expect_fail('invalid UUID')

    cg.server:exec(recreate_sort_data_file, {
        {instance = 'Instance: 00000000-0000-0000-0000-000000000000'}
    })
    expect_fail('unmatched UUID')

    cg.server:exec(recreate_sort_data_file, {{vclock = 'Invalid VClock key'}})
    expect_fail('file header read failed')

    cg.server:exec(recreate_sort_data_file, {{vclock = 'VClock: invalid'}})
    expect_fail('invalid VClock')

    cg.server:exec(recreate_sort_data_file, {{vclock = 'VClock: {1: 42}'}})
    expect_fail('unmatched VClock')

    cg.server:exec(recreate_sort_data_file, {{entries = 'Invalid entries key'}})
    expect_fail('invalid entry count')

    cg.server:exec(recreate_sort_data_file, {{entries = 'Entries: invalid'}})
    expect_fail('invalid entry count')

    -- Incomplete PK data.
    cg.server:exec(recreate_sort_data_file, {{pk_ptrs = '00000001'}})
    expect_panic('Invalid MemTX sort data file')

    -- Incomplete SK data.
    cg.server:exec(recreate_sort_data_file, {{sk_ptrs = '00000001'}})
    expect_panic('Invalid MemTX sort data file')

    -- No SK data.
    cg.server:exec(recreate_sort_data_file, {{sk_ptrs = ''}})
    expect_panic('Invalid MemTX sort data file')

    -- Incomplete PK and unexistng SK.
    cg.server:exec(recreate_sort_data_file, {{pk_ptrs = '00000001',
                                              sk_ptrs = ''}})
    expect_panic('Invalid MemTX sort data file')

    -- No data.
    cg.server:exec(recreate_sort_data_file, {{pk_ptrs = '', sk_ptrs = ''}})
    expect_panic('Invalid MemTX sort data file')

    -- Unexisting pointer.
    cg.server:exec(recreate_sort_data_file, {{sk_ptrs = '1111111100000002'}})
    expect_panic('Invalid MemTX sort data file')

    -- OK.
    cg.server:exec(recreate_sort_data_file, {{}})
    expect_success()
end

g_mvcc.before_each(function(cg)
    cg.server = server:new({
        box_cfg = {memtx_use_mvcc_engine = true,
                   memtx_use_sort_data = true},
        alias = 'master'
    })
    cg.server:start()
end)

g_mvcc.after_each(function(cg)
    cg.server:drop()
end)

-- Test that the snapshot written does not contain dirty data.
g_mvcc.test_snapshot = function(cg)
    -- Create the space and indexes and write snapshot with sort data.
    cg.server:exec(function(pk_type)
        local s = box.schema.create_space('test')
        assert(pk_type == 'TREE' or pk_type == 'HASH')
        s:create_index('pk', {type = pk_type, parts = {1, 'unsigned'}})
        s:create_index('sk', {type = 'TREE', parts = {2, 'unsigned'},
                              unique = false})

        local txn_proxy = require('test.box.lua.txn_proxy')
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()

        -- Start two concurrent transactions.
        tx1:begin()
        tx2:begin()

        -- Make independent changes.
        for i = 1, 100 do
            local random_sk_part_value = math.random(1, 1000000)

            -- Only insert even keys by TX1.
            if random_sk_part_value % 2 == 0 then
                tx1(string.format('box.space.test:insert({%d, %d})',
                                  i, random_sk_part_value))
            end

            -- Insert everything by TX2.
            tx2(string.format('box.space.test:replace({%d, %d})',
                              i, random_sk_part_value))
        end

        -- Apply tx1 changes.
        tx1:commit()

        -- Create a snapshot
        tx1('box.snapshot()')

        -- Get rid of tx2 changes after the checkpoint is done.
        tx2:rollback()
    end, {cg.params.pk_type})

    -- Start using the sort data.
    cg.server:restart()
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'sk\' of space \'test\''))

    -- Check that we only have tx1 changes saved into the snapshot.
    cg.server:exec(function()
        local fiber = require('fiber')
        for _, tuple in box.space.test.index.sk:pairs() do
            -- Check that only odd keys are written.
            t.assert(tuple[2] % 2 == 0)
        end
        fiber.yield()
    end)
end

g_graceful_shutdown.before_each(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_sort_data = true}})
    cg.server:start()
end)

g_graceful_shutdown.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

-- Test shutdown does not hang due to memtx space snapshot in progress.
g_graceful_shutdown.test_shutdown_during_memtx_snapshot = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        box.schema.create_space('test')
        box.space.test:create_index('pk')
        fiber.set_slice(100)
        box.begin()
        for i= 1, 30000 do
            box.space.test:insert{i}
        end
        box.commit()
        box.error.injection.set('ERRINJ_SORTDATA_WRITE_TIMEOUT', 0.01)
        fiber.create(function()
            box.snapshot()
        end)
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('saving snapshot'))
    end)

    -- Make random delay to check we are able to shutdown
    -- with snapshot in progress at any time.
    local fiber = require('fiber')
    fiber.sleep(math.random() * 3)

    -- Wait for the instance to shutdown.
    local channel = fiber.channel()
    fiber.create(function()
        server:stop()
        channel:put('finished')
    end)
    t.assert(channel:get(60) ~= nil)
end
