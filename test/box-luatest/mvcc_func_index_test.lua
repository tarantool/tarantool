local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
    cg.server:exec(function()
        local function table_values_are_zeros(table)
            for _, v in pairs(table) do
                if type(v) ~= 'table' then
                    if v ~= 0 then
                        return false
                    end
                else
                    if not table_values_are_zeros(v) then
                        return false
                    end
                end
            end
            return true
        end

        -- Checks if there are some tuple stories in memtx MVCC.
        local function mvcc_check_has_tuple_stories()
            local tuple_stories = box.stat.memtx.tx().mvcc.tuples
            t.assert(not table_values_are_zeros(tuple_stories))
        end
        rawset(_G, 'mvcc_check_has_tuple_stories', mvcc_check_has_tuple_stories)

        -- Clear all MVCC stories and check if they were really deleted.
        local function mvcc_clear_stories()
            -- A lot of steps to surely delete all stories.
            -- Each no-op step (when there are no stories) is cheap anyway.
            box.internal.memtx_tx_gc(100)
            local tuple_stories = box.stat.memtx.tx().mvcc.tuples
            t.assert(table_values_are_zeros(tuple_stories))
        end
        rawset(_G, 'mvcc_clear_stories', mvcc_clear_stories)
    end)
end)

g.after_each(function(cg)
    cg.server:drop()
end)

-- The case covers a crash when MVCC called a Lua function of functional
-- index on shutdown after Tarantool Lua state was released.
g.test_crash_on_shutdown = function(cg)
    cg.server:exec(function()
        local mvcc_check_has_tuple_stories =
            rawget(_G, 'mvcc_check_has_tuple_stories')

        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                return {tuple[1]}
            end]]
        })

        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('func', {
            func = 'test',
            parts = {{1, 'unsigned'}},
        })

        s:replace{1}
        s:delete{1}
        -- Check if there are some tuple stories right before shutdown.
        mvcc_check_has_tuple_stories()
    end)
end

-- The case covers an assertion failure when MVCC deleted a tuple from func
-- index and checked result of the deletion but the index didn't set the result.
g.test_crash_on_deletion = function(cg)
    cg.server:exec(function()
        local mvcc_clear_stories = rawget(_G, 'mvcc_clear_stories')

        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                return {tuple[1]}
            end]]
        })

        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('func', {
            func = 'test',
            parts = {{1, 'unsigned'}},
        })

        -- A bunch of simple replaces and deletions.
        for i = 1, 10 do
            s:replace{i}
            s:delete{i}
        end
        -- Clear all stories to check if they were deleted successfully.
        mvcc_clear_stories()
    end)
end

-- Reproducer from the issue.
-- See https://github.com/tarantool/tarantool/issues/10775.
g.test_crash_on_ffi_sandwich = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                return {tuple[1]}
            end]]
        })

        -- Simple space for which trace is recorded.
        local simple = box.schema.space.create('simple')
        simple:create_index('primary', {type = 'hash'})

        simple:replace{1}
        simple:replace{2}

        local s = box.schema.space.create('test', {format = {{'key', 'unsigned'},}})
        s:create_index('primary')

        local function pairs_loop(space)
            -- Big table with content of pairs, preallocated to avoid
            -- aborting of the trace.
            local tab = table.new(1024, 0)
            for _ = 1, 100 do
                for _, i in space:pairs(nil, {fullscan = true}) do
                    table.insert(tab, i)
                end
            end
            return tab
        end

        -- Setup VM state.
        collectgarbage()
        collectgarbage()
        jit.flush()
        jit.opt.start('hotloop=1')

        -- Stop LuaJIT GC to record trace properly.
        collectgarbage('stop')
        pairs_loop(simple)
        -- No need for future recording.
        jit.off()
        -- Restart LuaJIT GC to avoid OOM.
        collectgarbage('restart')

        s:create_index('value', {
            func = 'test',
            parts = {{1, 'unsigned'}},
        })

        -- Trigger memtx GC.

        s:replace{1}
        s:replace{2}

        local reader = fiber.create(function()
            box.begin()
            -- Select from the space with findex.
            s:select(nil, {fullscan = true})
            fiber.sleep(1)
            box.commit()
        end)
        reader:set_joinable(true)

        box.begin()
        s:delete(1)
        box.commit()

        reader:join()

        box.begin()
        -- Do select for the simple space again.
        pairs_loop(simple)
        box.commit()
    end)
end

-- The case checks if the function of func index is not called
-- when reading from another index. It is dangerous because we
-- are not allowed to call Lua function under FFI. For details,
-- see https://github.com/tarantool/tarantool/issues/10775.
g.test_no_function_call_on_non_func_read = function(cg)
    cg.server:exec(function()
        local mvcc_check_has_tuple_stories =
            rawget(_G, 'mvcc_check_has_tuple_stories')

        rawset(_G, 'counter', 0)

        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                counter = counter + 1
                return {tuple[1]}
            end]]
        })

        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('func', {
            func = 'test',
            parts = {{1, 'unsigned'}},
        })

        box.begin()
        -- Create dataset with gaps.
        for i = 1, 1000, 2 do
            s:replace{i}
            s:delete(i)
        end
        box.commit()
        mvcc_check_has_tuple_stories()

        rawset(_G, 'counter', 0)
        -- Many iterations to be more sure about covering the problem.
        for i = 1, 100 do
            -- Cover all methods with FFI implementation in the following way:
            -- 1. Open transaction so that GC won't be triggered on auto-commit.
            -- 2. Insert a new tuple to create a story and accumulate GC steps.
            -- 3. Call tested method with GC steps accumulated.
            -- 4. Rollback to revert changes.
            box.begin()
            s:replace{1000 + i}
            rawset(_G, 'counter', 0)
            s:select(i, {iterator = 'GT'})
            t.assert_equals(rawget(_G, 'counter'), 0)
            box.rollback()

            box.begin()
            s:replace{1000 + i}
            rawset(_G, 'counter', 0)
            s:pairs(i, {iterator = 'GT'}):totable()
            t.assert_equals(rawget(_G, 'counter'), 0)
            box.rollback()

            box.begin()
            s:replace{100 + i}
            rawset(_G, 'counter', 0)
            s:count(i, {iterator = 'GT'})
            t.assert_equals(rawget(_G, 'counter'), 0)
            box.rollback()

            box.begin()
            s:replace{1000 + i}
            rawset(_G, 'counter', 0)
            s.index.pk:random()
            t.assert_equals(rawget(_G, 'counter'), 0)
            box.rollback()

            box.begin()
            rawset(_G, 'counter', 0)
            s.index.pk:min()
            t.assert_equals(rawget(_G, 'counter'), 0)
            box.rollback()

            box.begin()
            s:replace{1000 + i}
            rawset(_G, 'counter', 0)
            s.index.pk:max()
            t.assert_equals(rawget(_G, 'counter'), 0)
            box.rollback()

            -- Point read of existing tuple.
            box.begin()
            s:replace{1000 + i}
            rawset(_G, 'counter', 0)
            s:get{1000 + i}
            t.assert_equals(rawget(_G, 'counter'), 0)
            box.rollback()

            -- Point read of non-existing tuple.
            box.begin()
            s:replace{1000 + i}
            rawset(_G, 'counter', 0)
            s:get{2000 + i}
            t.assert_equals(rawget(_G, 'counter'), 0)
            box.rollback()
        end
    end)
end
