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
        s:replace{1}

        -- Create functional index and delete the tuple
        s:create_index('func', {
            func = 'test',
            parts = {{1, 'unsigned'}},
        })
        s:delete{1}
        -- Check if there are some tuple stories right before shutdown.
        mvcc_check_has_tuple_stories()
    end)
end

g.test_func_index_point_hole_write = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')
        local txn = txn_proxy.new()

        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                return {tuple[2]}
            end]]
        })

        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('func', {
            func = 'test',
            parts = {{1, 'unsigned'}},
        })

        box.begin()
        t.assert_equals(s.index.func:get{10}, nil)
        txn:begin()
        txn('box.space.test:replace{1, 10}')
        t.assert_equals(s.index.func:get{10}, nil)
        txn:commit()
        t.assert_equals(s.index.func:get{10}, nil)
        box.commit()
    end)
end

g.test_func_index_count = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')
        local txn = txn_proxy.new()

        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                return {tuple[2]}
            end]]
        })

        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('func', {
            func = 'test',
            parts = {{1, 'unsigned'}},
        })

        box.begin()
        t.assert_equals(s.index.func:count(1, {iterator = 'GE'}), 0)
        txn:begin()
        txn('box.space.test:insert{1, 10}')
        t.assert_equals(s.index.func:count(1, {iterator = 'GE'}), 0)
        txn:commit()
        t.assert_equals(s.index.func:count(1, {iterator = 'GE'}), 0)
        box.commit()
    end)
end

g.test_func_index_offset = function(cg)
    cg.server:exec(function()
        local txn_proxy = require('test.box.lua.txn_proxy')
        local txn = txn_proxy.new()

        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                return {tuple[2]}
            end]]
        })

        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('func', {
            func = 'test',
            parts = {{1, 'unsigned'}},
        })

        s:replace{1, 10}
        s:replace{3, 8}

        box.begin()
        t.assert_equals(s.index.func:select(nil, {offset = 1}), {{1, 10}})
        txn:begin()
        txn('box.space.test:insert{2, 9}')
        t.assert_equals(s.index.func:select(nil, {offset = 1}), {{1, 10}})
        txn:commit()
        t.assert_equals(s.index.func:select(nil, {offset = 1}), {{1, 10}})
        box.commit()
    end)
end

g.test_func_exclude_null = function(cg)
    cg.server:exec(function()
        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                return {tuple[3]}
            end]]
        })

        local s = box.schema.space.create('test')
        s:create_index('primary', {parts = {{2, 'unsigned'}}})
        s:create_index('func', {
            func = 'test',
            parts = {{1, 'unsigned', is_nullable = true, exclude_null = true}},
        })

        s:replace{box.NULL, 1, 1}
        s:replace{box.NULL, 2, box.NULL}
        t.assert_equals(s.index.func:select(), {{nil, 1, 1}})
        t.assert_equals(s.index.func:count(), 1)
    end)
end

g.test_func_index_no_ffi_with_mvcc = function(cg)
    cg.server:exec(function()
        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                return {tuple[2]}
            end]]
        })

        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('func', {
            func = 'test',
            parts = {{1, 'unsigned'}},
        })

        t.assert_equals(s.index.pk.get, s.index.pk.get_ffi)
        -- Func index must use LuaC when MVCC is enabled.
        t.assert_equals(s.index.func.get, s.index.func.get_luac)
    end)

    -- Restart server in order to disable MVCC.
    cg.server:restart({box_cfg = {memtx_use_mvcc_engine = false}})
    cg.server:exec(function()
        local s = box.space.test
        t.assert_equals(s.index.pk.get, s.index.pk.get_ffi)
        -- Func index should use FFI when MVCC is disabled.
        t.assert_equals(s.index.func.get, s.index.func.get_ffi)
    end)
end

-- The case checks MVCC efficiency - the function must be called once
-- for each replace, not more than that.
g.test_one_func_call_per_replace = function(cg)
    cg.server:exec(function()
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

        t.assert_equals(rawget(_G, 'counter'), 0)
        for i = 1, 10 do
            s:replace{i}
            -- One function call for each replace.
            t.assert_equals(rawget(_G, 'counter'), i)
        end
    end)
end
