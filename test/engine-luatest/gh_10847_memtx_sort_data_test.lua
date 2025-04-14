local server = require('luatest.server')
local t = require('luatest')

local g_sk_types = t.group('memtx_sort_data_test-sk-types', t.helpers.matrix {
    pk_type = {'TREE', 'HASH'},
    sk_part_type = {'unsigned', 'string'},
    sk_part_sort_order = {'asc', 'desc'},
    sk_extra = {
        {hint = true},
        {hint = false},
        {path = 'scalar'},
        {path = 'array[*]'}
    }
})
local g_generic = t.group('memtx_sort_data_test-generic')
local g_mvcc = t.group('memtx_sort_data_test-mvcc',
                       {{pk_type = 'TREE'}, {pk_type = 'HASH'}})

g_sk_types.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {memtx_sort_data_enabled = true},
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
    cg.server:exec(function(pk_type, sk_part_type,
                            sk_part_sort_order, sk_extra)
        local s = box.schema.create_space('test')
        s:create_index('pk', {type = pk_type, parts = {1, 'unsigned'}})
        s:create_index('sk', {
            type = 'tree', hint = sk_extra.hint, unique = false,
            parts = {2, sk_part_type, sort_order = sk_part_sort_order,
                     path = sk_extra.path},
        })

        local function sk_object()
            local function sk_value()
                local max = 1000000
                if sk_part_type == 'unsigned' then
                    return math.random(1, max)
                elseif sk_part_type == 'string' then
                    return tostring(math.random(1, max))
                else
                    t.assert(false) -- can't insert this type
                end
            end

            if sk_extra.path ~= nil then
                local count = math.random(1, 3)
                local array = {}
                for i = 1, count do -- luacheck: no unused
                    table.insert(array, sk_value())
                end
                return {array = array, scalar = sk_value()}
            else
                return sk_value()
            end
        end

        for i = 1, 1000 do
            s:insert({i, sk_object()})
        end

        box.snapshot()
    end, {cg.params.pk_type, cg.params.sk_part_type,
          cg.params.sk_part_sort_order, cg.params.sk_extra})

    -- Start using the sort data.
    cg.server:restart()
    t.assert(cg.server:grep_log('Using MemTX sort data for building '..
                                'index \'sk\' of space \'test\''))

    -- Check the order of tuples in SK.
    cg.server:exec(function(sk_is_mk)
        local s = box.space.test
        t.assert_equals(s.index.pk:len(), 1000)

        if sk_is_mk then
            t.assert_ge(s.index.sk:len(), 1000)
            local processed = 0
            for _, tuple in s.index.sk:pairs() do
                -- Can't verify tuple order, only access
                -- its fields and verify to have no crash.
                t.assert(tuple[2].array ~= nil)
                processed = processed + 1
            end
            t.assert_equals(processed, s.index.sk:len())
            return
        end

        t.assert_equals(s.index.sk:len(), 1000)
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
    end, {cg.params.sk_extra.path ~= nil and
          string.find(cg.params.sk_extra.path, '*')})
end

g_generic.before_each(function(cg)
    -- Run the server with memtx sort data enabled.
    cg.server = server:new({
        box_cfg = {memtx_sort_data_enabled = true},
        alias = 'master'
    })
    cg.server:start()

    -- Create a space with data.
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:create_index('sk')
        for i = 1, 1000 do
            s:insert({i})
        end
    end)
end)

g_generic.after_each(function(cg)
    -- Drop the server.
    cg.server:drop()
end)

-- Test that a before_replace trigger on _index space prevent using sort data.
g_generic.test_index_before_replace = function(cg)
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
    local msg = 'memtx_sort_data_enabled = true but no memtx sort data used:' ..
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

-- Test that before_replace triggers prevent using the space sort data.
g_generic.test_space_before_replace = function(cg)
    -- Create a snapshot and sort data file.
    cg.server:exec(function()
        box.snapshot()
    end)

    -- Load it with the test.before_recovery_replace trigger set up.
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

    -- The before_recovery_replace trigger on a space must
    -- prevent the new index sorting method to be used on it.
    local msg = 'Skipped sort data for space \'test\':' ..
                ' the space has before_replace triggers.'
    t.assert(cg.server:grep_log(msg))

    -- Load it with a regular test.before_replace trigger set up.
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

    -- Now it's OK since the trigger is not called on recovery.
    t.assert(cg.server:grep_log('Using MemTX sort data for building '..
                                'index \'sk\' of space \'test\''))
end

-- Test that the sort data works for multiple spaces.
g_generic.test_multiple_spaces = function(cg)
    -- Create the second space with data, save a snapshot and load it.
    cg.server:exec(function()
        local s = box.schema.create_space('test2')
        s:create_index('pk')
        s:create_index('sk')
        for i = 1, 1000 do
            s:insert({i})
        end
        box.snapshot()
    end)
    cg.server:restart()

    -- We must both spaces loaded using the sort data.
    local log_format = 'Using MemTX sort data for building '..
                       'index \'sk\' of space \'%s\''
    local log_test = string.format(log_format, 'test')
    local log_test2 = string.format(log_format, 'test2')
    t.assert(cg.server:grep_log(log_test))
    t.assert(cg.server:grep_log(log_test2))
end

-- Test that existence of vinyl spaces does not break the recovery.
g_generic.test_vinyl_space_ignored = function(cg)
    -- Create a vinyl space with data, save a snapshot and load it.
    cg.server:exec(function()
        local s = box.schema.create_space('test_vinyl', {engine = 'vinyl'})
        s:create_index('pk')
        s:create_index('sk')
        for i = 1, 1000 do
            s:insert({i})
        end
        box.snapshot()
    end)
    cg.server:restart()

    -- We must have memtx space loaded using the sort data.
    local log_format = 'Using MemTX sort data for building '..
                       'index \'sk\' of space \'%s\''
    local log_memtx = string.format(log_format, 'test')
    local log_vinyl = string.format(log_format, 'test_vinyl')
    t.assert(cg.server:grep_log(log_memtx) ~= nil)
    t.assert(cg.server:grep_log(log_vinyl) == nil)
end

-- Test recovery using both snapshot and WAL.
g_generic.test_recover_with_wal = function(cg)
    cg.server:exec(function()
        -- Create a snapshot and sort data file.
        box.snapshot()

        -- Perform more operations to create a WAL log.
        for i = 1, 1000 do
            box.space.test:insert({1000 + i})
        end
    end)

    -- Recover from the snapshot and WAL.
    cg.server:restart()
    t.assert(cg.server:grep_log('Using MemTX sort data for building '..
                                'index \'sk\' of space \'test\''))
    t.assert(cg.server:grep_log('entering the event loop'))
end

-- Test recovery without sort data file if memtx_sort_data_enabled = true.
g_generic.test_recover_without_sort_data = function(cg)
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
    t.assert(cg.server:grep_log('memtx_sort_data_enabled = true but' ..
                                ' no memtx sort data file found'))
    t.assert(cg.server:grep_log('Space \'test\': done'))
    t.assert(cg.server:grep_log('entering the event loop'))
end

-- Test that the sort data file is not recreated on box.snapshot after removal.
g_generic.test_remove_sortdata_keep_snap = function(cg)
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
        t.assert_error_msg_contains('.sortdata\' already exists', box.snapshot)
        t.assert(#fio.glob(glob_snap) == 0)
        t.assert(#fio.glob(glob_sortdata) == 1)
    end)
end

-- Test that both snapshot and sort data file recreated if are absent.
g_generic.test_remove_snap_and_sortdata = function(cg)
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
    -- One way to fail the .snap file creation - is go out of file descriptors.
    -- Let's try it in case the system has a sanely low ulimit.
    local nfile
    local ulimit = io.popen('ulimit -n')
    if ulimit then
        nfile = tonumber(ulimit:read())
        ulimit:close()
    end
    if not nfile or nfile > 1024 then
        -- Descriptors limit is to high, skip the test.
        return
    end

    cg.server:exec(function()
        -- Open files to approach the descriptors limit.
        local fio = require('fio')
        local files = {}
        local count = 0
        repeat
            count = count + 1
            files[count] = fio.open('/dev/null')
        until files[count] == nil

        -- The checkpoint is aborted successfully.
        local msg = 'snap.inprogress\': Too many open files'
        t.assert_error_msg_contains(msg, box.snapshot)

        -- Close the files.
        for _, file in pairs(files) do
            file:close()
        end
    end)
end

-- Test that checkpoint abortion discards the written .sortdata file.
g_generic.test_snapshot_abort_discards_sortdata_file = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fio = require('fio')
        local fiber = require('fiber')
        local function wait_for_file(filename)
            repeat
                fiber.sleep(0.01)
            until fio.path.exists(filename)
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

g_mvcc.before_each(function(cg)
    cg.server = server:new({
        box_cfg = {memtx_use_mvcc_engine = true,
                   memtx_sort_data_enabled = true},
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
        for i = 1, 1000 do
            local random_sk_part_value = math.random(1, 1000000)
            local query = string.format('box.space.test:insert({%d, %d})',
                                        i, random_sk_part_value)
            if random_sk_part_value % 2 == 0 then
                tx1(query)
            else
                tx2(query)
            end
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
    t.assert(cg.server:grep_log('Using MemTX sort data for building '..
                                'index \'sk\' of space \'test\''))

    -- Check that we only have tx1 changes saved into the snapshot.
    cg.server:exec(function()
        for _, tuple in box.space.test:pairs() do
            t.assert(tuple[2] % 2 == 0)
        end
    end)
end
