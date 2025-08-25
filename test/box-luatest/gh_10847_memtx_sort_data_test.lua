local server = require('luatest.server')
local t = require('luatest')

local g_generic = t.group('memtx_sort_data_test-generic')
local g_invalid_file = t.group('memtx_sort_data_test-invalid_file')
local g_mvcc = t.group('memtx_sort_data_test-mvcc')

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

-- Test the sort data machinery works appropriately.
g_generic.test_index_build = function(cg)
    -- Create the space and indexes and write snapshot with sort data.
    cg.server:exec(function()
        -- Space with TREE primary key.
        local s = box.schema.create_space('test')
        s:create_index('pk', {parts = {1, 'unsigned'}})

        -- Tree with hints.
        s:create_index('i1', {hint = true, unique = false,
                              parts = {2, 'unsigned'}})

        -- Tree without hints.
        s:create_index('i2', {hint = false, unique = false,
                              parts = {2, 'unsigned'}})

        -- Nullable part.
        s:create_index('i3', {unique = false,
                              parts = {3, 'unsigned', is_nullable = true}})

        -- Nullable with nulls excluded.
        s:create_index('i4', {unique = false,
                              parts = {3, 'unsigned', is_nullable = true,
                                       exclude_null = true}})

        -- Empty nullable with nulls excluded.
        s:create_index('i4e', {unique = false,
                               parts = {4, 'unsigned', is_nullable = true,
                                        exclude_null = true}})


        -- Multikey index.
        s:create_index('i5', {unique = false,
                              parts = {5, 'unsigned', path = 'array[*]'}})

        -- Multikey with nullable part.
        s:create_index('i6', {unique = false,
                              parts = {6, 'unsigned', path = 'array[*]',
                                       is_nullable = true}})

        -- Multikey with nullable part and excluded nulls.
        s:create_index('i7', {unique = false,
                              parts = {6, 'unsigned', path = 'array[*]',
                                       is_nullable = true,
                                       exclude_null = true}})

        -- Functional key.
        local func_lua_code = 'function(tuple) return {tuple[1]} end'
        box.schema.func.create('func', {body = func_lua_code,
                                        is_deterministic = true,
                                        is_sandboxed = true})
        s:create_index('fk', {func = 'func',
                              parts = {1, 'unsigned', sort_order = 'desc'}})

        -- Fill the space with corresponding data.
        for i = 1, 50 do
            local random_value = math.random(1000)
            -- ~33% of nulls.
            local nullable_value =
                math.random(1, 3) == 1 and box.NULL or math.random(1000)
            local random_array = {math.random(1000), math.random(1000)}
            local nullable_array = {
                math.random(1, 3) == 1 and box.NULL or math.random(1000),
                math.random(1, 3) == 1 and box.NULL or math.random(1000),
            }
            s:insert({i, random_value, nullable_value, nil,
                      {array = random_array}, {array = nullable_array}})
        end

        -- Space with HASH primary key.
        local s2 = box.schema.create_space('test2')
        s2:create_index('pk', {type = 'HASH'})
        s2:create_index('sk', {unique = false, parts = {2, 'unsigned'}})
        for i = 1, 50 do
            s2:insert({i, math.random(1000)})
        end

        -- Local space.
        local s3 = box.schema.create_space('test_local', {is_local = true})
        s3:create_index('pk')
        s3:create_index('sk', {parts = {1, 'unsigned', sort_order = 'desc'}})
        for i = 1, 50 do
            s3:insert({i})
        end

        -- Vinyl space.
        local s4 = box.schema.create_space('test_vinyl', {engine = 'vinyl'})
        s4:create_index('pk')
        s4:create_index('sk')
        for i = 1, 50 do
            s4:insert({i})
        end

        -- Create snapshot.
        box.snapshot()

        -- Create WAL.
        for i = 1, 50 do
            s3:insert({50 + i})
        end
    end)

    -- Start using the sort data.
    cg.server:restart()
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'i1\' of space \'test\''))
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'i2\' of space \'test\''))
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'i3\' of space \'test\''))
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'i4\' of space \'test\''))
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'i4e\' of space \'test\''))
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'i5\' of space \'test\''))
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'i6\' of space \'test\''))
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'i7\' of space \'test\''))
    -- Must NOT use the sort data on a functional SK.
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'fk\' of space \'test\'') == nil)

    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'sk\' of space \'test2\''))
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'sk\' of space \'test_local\''))
    -- Must NOT use the sort data on a vinyl index.
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'sk\' of space \'test_vinyl\'') == nil)

    -- Check the order of tuples in SK.
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        local s2 = box.space.test2
        local s3 = box.space.test_local
        local s4 = box.space.test_vinyl

        -- Check PK.
        t.assert_equals(s.index.pk:len(), 50)

        -- Check SK with hints.
        t.assert_equals(s.index.i1:len(), 50)
        local prev_value = nil
        for _, tuple in s.index.i1:pairs() do
            if prev_value ~= nil then
                t.assert_ge(tuple[2], prev_value)
            end
            prev_value = tuple[2]
        end
        fiber.yield()

        -- Check SK without hints.
        t.assert_equals(s.index.i2:len(), 50)
        prev_value = nil
        for _, tuple in s.index.i2:pairs() do
            if prev_value ~= nil then
                t.assert_ge(tuple[2], prev_value)
            end
            prev_value = tuple[2]
        end
        fiber.yield()

        -- Check nullable SK.
        t.assert_equals(s.index.i3:len(), 50)
        prev_value = nil
        for _, tuple in s.index.i3:pairs() do
            if prev_value ~= nil then
                t.assert_ge(tuple[3], prev_value)
            end
            -- First ~33% values expected to be nulls.
            prev_value = tuple[3]
        end
        fiber.yield()

        -- Check nullable SK with nulls excluded.
        t.assert_lt(s.index.i4:len(), 50)
        prev_value = nil
        for _, tuple in s.index.i4:pairs() do
            if prev_value ~= nil then
                t.assert_ge(tuple[3], prev_value)
            end
            prev_value = tuple[3]
        end
        fiber.yield()

        -- Check the empty nullable SK with nulls excluded.
        t.assert_equals(s.index.i4e:len(), 0)

        -- Check multikey SK. Multikey checks aren't precise.
        t.assert_le(s.index.i5:len(), 100)
        local prev_value = nil
        for _, tuple in s.index.i5:pairs() do
            if prev_value ~= nil then
                local elem_1_is_ge = tuple[5].array[1] >= prev_value
                local elem_2_is_ge = tuple[5].array[2] >= prev_value
                t.assert(elem_1_is_ge or elem_2_is_ge)
                if elem_1_is_ge ~= elem_2_is_ge then
                    -- Only one is greater or equal - the MAX one.
                    prev_value = math.max(tuple[5].array[1], tuple[5].array[2])
                else
                    -- Both are greater or equal, peek the smallest.
                    prev_value = math.min(tuple[5].array[1], tuple[5].array[2])
                end
            else
                prev_value = math.min(tuple[5].array[1], tuple[5].array[2])
            end
        end
        fiber.yield()

        -- GE check with nulls.
        local function cmp(a, b)
            if a ~= nil and b ~= nil then
                -- Compare regular numbers.
                return a - b
            elseif a == nil and b == nil then
                -- Both nulls (equal).
                return 0
            else
                -- If a not nil then it's greater than b (nil)
                assert((a == nil) ~= (b == nil))
                return a ~= nil and 1 or -1
            end
        end

        -- Check nullable multikey SK.
        t.assert_le(s.index.i6:len(), 100)
        local prev_value = nil
        for _, tuple in s.index.i6:pairs() do
            local elem_1_is_ge = cmp(tuple[6].array[1], prev_value) >= 0
            local elem_2_is_ge = cmp(tuple[6].array[2], prev_value) >= 0
            local elem_max = cmp(tuple[6].array[1], tuple[6].array[2]) > 0
                             and tuple[6].array[1] or tuple[6].array[2]
            local elem_min = cmp(tuple[6].array[1], tuple[6].array[2]) < 0
                             and tuple[6].array[1] or tuple[6].array[2]
            t.assert(elem_1_is_ge or elem_2_is_ge)
            if elem_1_is_ge ~= elem_2_is_ge then
                -- Only one is greater or equal - the MAX one.
                prev_value = elem_max
            else
                -- Both are greater or equal, peek the smallest.
                prev_value = elem_min
            end
        end
        fiber.yield()

        -- Check nullable multikey SK with nulls excluded.
        t.assert_le(s.index.i7:len(), 100)
        local prev_value = nil
        for _, tuple in s.index.i7:pairs() do
            local elem_1_is_ge = cmp(tuple[6].array[1], prev_value) >= 0
            local elem_2_is_ge = cmp(tuple[6].array[2], prev_value) >= 0
            local elem_max = cmp(tuple[6].array[1], tuple[6].array[2]) > 0
                             and tuple[6].array[1] or tuple[6].array[2]
            local elem_min = cmp(tuple[6].array[1], tuple[6].array[2]) < 0
                             and tuple[6].array[1] or tuple[6].array[2]
            t.assert(elem_1_is_ge or elem_2_is_ge)
            if elem_1_is_ge ~= elem_2_is_ge then
                -- Only one is greater or equal - the MAX one.
                prev_value = elem_max
            else
                -- Both are greater or equal, peek the smallest.
                prev_value = elem_min
            end
        end
        fiber.yield()

        -- Check functional key.
        t.assert_equals(s.index.fk:len(), 50)
        local i = 50
        for _, tuple in s.index.fk:pairs() do
            t.assert_equals(tuple[1], i)
            i = i - 1
        end
        t.assert_equals(i, 0)
        fiber.yield()

        -- Check HASH PK and the space SK.
        t.assert_equals(s2.index.pk:len(), 50)
        t.assert_equals(s2.index.sk:len(), 50)
        local prev_value = nil
        for _, tuple in s2.index.sk:pairs() do
            if prev_value ~= nil then
                t.assert_ge(tuple[2], prev_value)
            end
            prev_value = tuple[2]
        end
        fiber.yield()

        -- Check local space.
        t.assert_equals(s3.index.pk:len(), 100)
        t.assert_equals(s3.index.sk:len(), 100)
        local i = 100
        for _, tuple in s3.index.sk:pairs() do
            t.assert_equals(tuple[1], i)
            i = i - 1
        end
        fiber.yield()

        -- Check vinyl space.
        t.assert_equals(s4.index.pk:len(), 50)
        t.assert_equals(s4.index.sk:len(), 50)
        t.assert_equals(i, 0)
        for _, tuple in s4.index.sk:pairs() do
            i = i + 1
            t.assert_equals(tuple[1], i)
        end
        t.assert_equals(i, 50)
        fiber.yield()
    end)
end

-- Test recovery conditions prohibiting using the new SK build
-- approach (e. g. triggers set, no sort data file exists).
g_generic.test_fallback_cases = function(cg)
    -- Create a space and dump a snapshot and sort data file.
    create_test_space(cg)
    cg.server:exec(function()
        box.snapshot()
    end)

    -- Load it with the _index.before_recovery_replace trigger set up.
    -- Must not use the sort data because of the trigger.
    cg.server:restart({env = {['TARANTOOL_RUN_BEFORE_BOX_CFG'] = [[
        local trigger = require('trigger')
        trigger.set('box.space.test.before_recovery_replace', 'test_trigger',
                    function(old, new, space, req, header, body)
                        return new
                    end)
    ]]}})
    local success_msg = 'Using MemTX sort data for building' ..
                        ' index \'sk\' of space \'test\''
    t.assert(cg.server:grep_log(success_msg) == nil)

    -- Now load it with a regular test.before_replace trigger set up. It should
    -- use the sort data file (the regular before_replace trigger does not fire
    -- on recovery so it should not affect the ability to use it).
    cg.server:restart({env = {['TARANTOOL_RUN_BEFORE_BOX_CFG'] = [[
        local trigger = require('trigger')
        trigger.set('box.space.test.before_replace', 'test_trigger',
                    function(old, new, space, req, header, body)
                        return new
                    end)
    ]]}})
    t.assert(cg.server:grep_log(success_msg))

    -- Now run with the _index.before_recovery_replace trigger set up.
    -- Must not use the sort data because of the trigger.
    cg.server:restart({env = {['TARANTOOL_RUN_BEFORE_BOX_CFG'] = [[
        local trigger = require('trigger')
        trigger.set('box.space._index.before_recovery_replace', 'test_trigger',
                    function(old, new, space, req, header, body)
                        return new
                    end)
    ]]}})
    t.assert(cg.server:grep_log('memtx_use_sort_data = true but no memtx ' ..
                                'sort data used: the _index space has ' ..
                                'before_replace triggers'))

    -- Now load it with a regular _index.before_replace trigger set up. It
    -- should use the sort data file (the regular before_replace trigger does
    -- not fire on recovery so it should not affect the ability to use it).
    cg.server:restart({env = {['TARANTOOL_RUN_BEFORE_BOX_CFG'] = [[
        local trigger = require('trigger')
        trigger.set('box.space._index.before_replace', 'test_trigger',
                    function(old, new, space, req, header, body)
                        return new
                    end)
    ]]}})
    t.assert(cg.server:grep_log(success_msg))

    -- Test recovery without sort data file if memtx_use_sort_data = true.
    cg.server:exec(function()
        -- Remove all sort data files.
        local fio = require('fio')
        local glob = fio.pathjoin(box.cfg.memtx_dir, '*.sortdata')
        local files = fio.glob(glob)
        t.assert(#files > 0) -- a sort data file must have been created
        for _, file in pairs(files) do
            fio.unlink(file)
        end
    end)
    cg.server:restart()
    t.assert(cg.server:grep_log('memtx_use_sort_data = true but' ..
                                ' no memtx sort data file found'))
    t.assert(cg.server:grep_log('Space \'test\': done'))
    t.assert(cg.server:grep_log('entering the event loop'))
end

-- Test the snapshot creation when sort data used.
g_generic.test_checkpoint = function(cg)
    create_test_space(cg)

    cg.server:exec(function()
        local fio = require('fio')
        local glob_snap = fio.pathjoin(box.cfg.memtx_dir, '*.snap')
        local glob_sortdata = fio.pathjoin(box.cfg.memtx_dir, '*.sortdata')

        local function remove_all(glob)
            local files = fio.glob(glob)
            t.assert(#files > 0)
            for _, file in pairs(files) do
                fio.unlink(file)
            end
        end

        -- Initial snapshot.
        box.snapshot()

        -- Sort data file is not recreated after removal.
        remove_all(glob_sortdata)
        box.snapshot()
        t.assert_ge(#fio.glob(glob_snap), 1)
        t.assert_equals(#fio.glob(glob_sortdata), 0)

        -- Both files recreated if are absent.
        remove_all(glob_snap)
        box.snapshot()
        t.assert_equals(#fio.glob(glob_snap), 1)
        t.assert_equals(#fio.glob(glob_sortdata), 1)

        -- Checkpoint fails if only .snap is absent.
        remove_all(glob_snap)
        t.assert_error_msg_contains('.sortdata\': File exists', box.snapshot)
        t.assert_equals(#fio.glob(glob_snap), 0)
        t.assert_equals(#fio.glob(glob_sortdata), 1)
    end)
end

-- Test various failures during the checkpoint.
g_generic.test_checkpoint_failures = function(cg)
    t.tarantool.skip_if_not_debug()
    create_test_space(cg)

    -- Test that .snap file creation failure is handled correctly.
    cg.server:exec(function()
        -- One way to fail the .snap file creation is to create a
        -- filesystem entry with the name of an in-progress snapshot.
        local fio = require('fio')
        local snap_filename = string.format('%020d.snap.inprogress',
                                             box.info.signature)
        local snap_path = fio.pathjoin(box.cfg.memtx_dir, snap_filename)
        fio.mkdir(snap_path)
        t.assert_error_msg_contains('.snap.inprogress\': File exists',
                                    box.snapshot)
        fio.rmdir(snap_path)
    end)

    -- Test that checkpoint abortion discards the written .sortdata file.
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

-- Test shutdown does not hang due to memtx space snapshot in progress.
g_generic.test_checkpoint_graceful_shutdown = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        box.schema.create_space('test')
        box.space.test:create_index('pk')
        box.space.test:create_index('sk')
        fiber.set_slice(100)
        box.begin()
        for i = 1, 30000 do
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
        assert(box.cfg.checkpoint_count == 2) -- The expected default value.
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

-- Test that an invalid sort data file is handled correctly.
g_invalid_file.test_invalid_file = function()
    local function expect_success(server)
        server:wait_until_ready()
        t.assert(server:grep_log('Using MemTX sort data for building ' ..
                                    'index \'sk\' of space \'test\''))
        t.assert(server:grep_log('entering the event loop'))
    end

    local function expect_fail(server, msg)
        server:wait_until_ready()
        t.assert(server:grep_log(msg))
        t.assert(server:grep_log('Adding 2 keys to TREE index \'sk\' ...'))
        t.assert(server:grep_log('entering the event loop'))
    end

    local function expect_panic(server, msg)
        local fio = require('fio')
        t.helpers.retrying({}, function()
            local filename = fio.pathjoin(server.workdir,
                                          server.alias .. '.log')
            t.assert(server:grep_log(msg, nil, {filename = filename}))
        end)
    end

    -- Create a space with data, checkpoint and update the sort data file.
    local function test_init(server, file_config)
        server:wait_until_ready()
        server:exec(function(file)
            -- Create a test space and checkpoint.
            local s = box.schema.create_space('test')
            s:create_index('pk')
            s:create_index('sk', {hint = false})
            s:insert({1})
            s:insert({2})
            box.snapshot()

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
            local entries_fmt = (file.entries or 'Entries: 0000000002') ..
                                '\n%010d/%010u: %016x, %016x, %20d\n' ..
                                '%010d/%010u: %016x, %016x, %20d\n\n'
            local entries_size = #string.format(entries_fmt, 0, 0, 0, 0, 0,
                                                             0, 0, 0, 0, 0)
            local pk_data = file.pk_ptrs or '0000000100000002' -- PK pointers.
            local sk_data = file.sk_ptrs or '0000000100000002' -- Same order.
            local pk_data_size = #pk_data
            local sk_data_size = #sk_data
            local pk_data_offset = header_size + entries_size
            local sk_data_offset = pk_data_offset + pk_data_size
            local entries = string.format(
                entries_fmt,
                s.id, s.index.pk.id, pk_data_offset, pk_data_size, 2,
                s.id, s.index.sk.id, sk_data_offset, sk_data_size, 2)
            assert(#entries == entries_size)

            -- Create the file.
            local f = fio.open(last_sort_data, {'O_WRONLY', 'O_CREAT'})
            f:write(header)
            f:write(entries)
            f:write(pk_data)
            f:write(sk_data)
            f:close()
        end, {file_config})
    end

    local tests = {
        {
            file = {file_magic = 'Invalid'},
            check = expect_fail,
            check_arg = 'file header read failed',
        },
        {
            file = {file_version = 'Invalid'},
            check = expect_fail,
            check_arg = 'file header read failed',
        },
        {
            file = {version = 'Invalid version key'},
            check = expect_fail,
            check_arg = 'file header read failed',
        },
        {
            file = {instance = 'Invalid uuid key'},
            check = expect_fail,
            check_arg = 'file header read failed',
        },
        {
            file = {instance = 'Instance: invalid'},
            check = expect_fail,
            check_arg = 'invalid UUID',
        },
        {
            file = {
                instance = 'Instance: 00000000-0000-0000-0000-000000000000'
            },
            check = expect_fail,
            check_arg = 'unmatched UUID',
        },
        {
            file = {vclock = 'Invalid VClock key'},
            check = expect_fail,
            check_arg = 'file header read failed',
        },
        {
            file = {vclock = 'VClock: invalid'},
            check = expect_fail,
            check_arg = 'invalid VClock',
        },
        {
            file = {vclock = 'VClock: {1: 42}'},
            check = expect_fail,
            check_arg = 'unmatched VClock',
        },
        {
            file = {entries = 'Invalid entries key'},
            check = expect_fail,
            check_arg = 'invalid entry count',
        },
        {
            file = {entries = 'Entries: invalid'},
            check = expect_fail,
            check_arg = 'invalid entry count',
        },
        {
            -- Incomplete PK data.
            file = {pk_ptrs = '00000001'},
            check = expect_panic,
            check_arg = 'Invalid MemTX sort data file',
        },
        {
            -- Incomplete SK data.
            file = {sk_ptrs = '00000001'},
            check = expect_panic,
            check_arg = 'Invalid MemTX sort data file',
        },
        {
            -- No SK data.
            file = {sk_ptrs = ''},
            check = expect_panic,
            check_arg = 'Invalid MemTX sort data file',
        },
        {
            -- Incomplete PK and unexistng SK.
            file = {pk_ptrs = '00000001', sk_ptrs = ''},
            check = expect_panic,
            check_arg = 'Invalid MemTX sort data file',
        },
        {
            -- No data.
            file = {pk_ptrs = '', sk_ptrs = ''},
            check = expect_panic,
            check_arg = 'Invalid MemTX sort data file',
        },
        {
            -- Unexisting pointer.
            file = {sk_ptrs = '1111111100000002'},
            check = expect_panic,
            check_arg = 'Invalid MemTX sort data file',
        },
        {
            -- OK.
            file = {},
            check = expect_success,
            check_arg = nil,
        },
    }

    -- Run test servers concurrently.
    for i, test in ipairs(tests) do
        test.server = server:new({
            box_cfg = {memtx_use_sort_data = true},
            alias = 'invalid_file_tester_' .. i,
        })
        test.server:start({wait_until_ready = false})
    end

    -- Initialize the servers and update their sort data files.
    for i, test in ipairs(tests) do -- luacheck: no unused
        test_init(test.server, test.file)
    end

    -- Restart servers concurrently.
    for i, test in ipairs(tests) do -- luacheck: no unused
        test.server:restart({}, {wait_until_ready = false})
    end

    -- Check the test results.
    for i, test in ipairs(tests) do -- luacheck: no unused
        test.check(test.server, test.check_arg)
    end
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
    cg.server:exec(function()
        -- Create a space with TREE primary key.
        local s1 = box.schema.create_space('tree')
        s1:create_index('pk', {type = 'TREE', parts = {1, 'unsigned'}})
        s1:create_index('sk', {type = 'TREE', parts = {2, 'unsigned'},
                               unique = false})

        -- Create a space with HASH primary key.
        local s2 = box.schema.create_space('hash')
        s2:create_index('pk', {type = 'HASH', parts = {1, 'unsigned'}})
        s2:create_index('sk', {type = 'TREE', parts = {2, 'unsigned'},
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
                tx1(string.format('box.space.tree:insert({%d, %d})',
                                  i, random_sk_part_value))
                tx1(string.format('box.space.hash:insert({%d, %d})',
                                  i, random_sk_part_value))
            end

            -- Insert everything by TX2.
            tx2(string.format('box.space.tree:replace({%d, %d})',
                              i, random_sk_part_value))
            tx2(string.format('box.space.hash:replace({%d, %d})',
                              i, random_sk_part_value))
        end

        -- Apply tx1 changes.
        tx1:commit()

        -- Create a snapshot
        box.snapshot()

        -- Get rid of tx2 changes after the checkpoint is done.
        tx2:rollback()
    end)

    -- Start using the sort data.
    cg.server:restart()
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'sk\' of space \'tree\''))
    t.assert(cg.server:grep_log('Using MemTX sort data for building ' ..
                                'index \'sk\' of space \'hash\''))

    -- Check that only tx1 changes (odd keys) saved into the snapshot.
    cg.server:exec(function()
        local fiber = require('fiber')
        for _, tuple in box.space.tree.index.sk:pairs() do
            t.assert(tuple[2] % 2 == 0)
        end
        fiber.yield()
        for _, tuple in box.space.hash.index.sk:pairs() do
            t.assert(tuple[2] % 2 == 0)
        end
        fiber.yield()
    end)
end
