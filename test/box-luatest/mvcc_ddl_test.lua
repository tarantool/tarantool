local server = require('luatest.server')
local t = require('luatest')

local g = t.group('MVCC DDL tests for known issues')

g.before_each(function(cg)
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:stop()
end)

-- The test checks that all delete statements are handled correctly
-- on space drop
g.test_drop_space = function(cg)
    cg.server:exec(function()
        local txn_proxy = require("test.box.lua.txn_proxy")

        -- Create space with tuples
        local s = box.schema.space.create('test')
        s:create_index('pk')
        for i = 1, 100 do
            s:replace{i}
        end

        -- Delete the tuples concurrently
        local tx1 = txn_proxy:new()
        local tx2 = txn_proxy:new()
        tx1:begin()
        tx2:begin()
        for i = 1, 100 do
            local stmt = "box.space.test:delete{" .. i .. "}"
            tx1(stmt)
            tx2(stmt)
        end
        s:drop()
        tx1:rollback()
        tx2:rollback()

        -- Collect garbage
        box.internal.memtx_tx_gc(1000)
    end)
end

-- gh-10147
g.test_background_build = function(cg)
    cg.server:exec(function()
        local txn_proxy = require("test.box.lua.txn_proxy")
        local fiber = require('fiber')

        -- Create space with tuples
        local s = box.schema.space.create('test')
        s:create_index('pk')
        for i = 1, 2000 do
            s:replace{i}
        end

        local index_built = false
        local f = fiber.create(function()
            s:create_index('sk')
            index_built = true
        end)
        f:set_joinable(true)

        -- Delete the tuples concurrently
        local tx1 = txn_proxy:new()
        tx1:begin()
        for i = 1, 2000 do
            local stmt = "box.space.test:delete{" .. i .. "}"
            tx1(stmt)
        end
        
        assert(not index_built)
        local ok = f:join()
        t.assert(ok)
        tx1:commit()

        -- Collect garbage
        box.internal.memtx_tx_gc(1000)
    end)
end

-- gh-10096
g.test_create_index = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1}}})
        s:create_index('sk1', {parts = {{2}}, unique = true})

        s:insert{1, 1, 1, 0}
        -- Collect stories to be independent from GC steps
        box.internal.memtx_tx_gc(100)

        -- Alter index in a separate transaction
        -- Transaction creating index will create story for our tuple
        -- when it reads it to insert into the new index
        -- The problem is the story link points to the old index, but
        -- it was replaced with the new one (new index object, hence,
        -- new address)
        local altered_index = false
        local f = fiber.create(function()
            s.index.sk1:alter({parts = {{3}}, unique = true})
            altered_index = true
        end)
        f:set_joinable(true)

        -- Make sure txn is still in progress
        assert(not altered_index)
        local msg = "Can't modify space that is being altered"
        t.assert_error_msg_content_equals(msg, function()
            s:replace{1, 1, 1, 1}
        end)
        f:join()
    end)
end

-- gh-10097
g.test_alter_index = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1}}})
        s:create_index('sk1', {parts = {{2}}, unique = true})

        s:insert{1, 1, 1, 0}
        -- Collect stories to be independent from GC steps
        box.internal.memtx_tx_gc(100)

        -- Create index in a separate transaction
        -- Transaction creating index will create story for our tuple
        -- when it reads it to insert into the new index
        -- The problem is the story is created with old index_count and
        -- does not have links for created index
        local created_index = false
        local f = fiber.create(function()
            s:create_index('sk2', {parts = {{3}}, unique = true})
            created_index = true
        end)
        f:set_joinable(true)

        -- Make sure txn is still in progress
        assert(not created_index)
        local msg = "Can't modify space that is being altered"
        t.assert_error_msg_content_equals(msg, function()
            s:replace{1, 1, 1, 1}
        end)
        f:join()
    end)
end

local g = t.group('Stress tests for MVCC DDL', t.helpers.matrix{
    is_interactive = {true, false},
    disable_background_index_build = {true, false},
})

g.before_each(function(cg)
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:stop()
end)

local function stress_test_impl(seed, interactive_dml)
    math.randomseed(seed)
    local fiber = require('fiber')
    local formats = {
        {{name = 'field1', type = 'unsigned'}},
        {{name = 'field1', type = 'scalar'}}
    }
    local next_format_idx = 1
    box.schema.space.create('test')
    box.space.test:create_index('pk')

    local MIN_INDEX = 2
    local MAX_INDEX = 10
    for i = 1, MAX_INDEX do
        box.space.test:create_index('sk_' .. tostring(i))
    end

    local MAX_TUPLE = math.random(500, 2000)
    for i = 1, MAX_TUPLE do
        box.space.test:replace{i}
    end

    local max_ddl_ops = 5
    local ddl_ops = {
        'alter space',
        'create index',
        'alter index',
        'drop index'
    }

    local max_dml_ops = 20
    local dml_ops = {
        'insert',
        'replace',
        'delete',
        'get'
    }

    local successfully_completed_dml = 0
    local successfully_completed_ddl = 0

    local function do_random_ddl()
        local idx = math.random(#ddl_ops)
        local op = ddl_ops[idx]

        if op == 'alter space' then
            local new_format = formats[next_format_idx]
            box.space.test:format(new_format)
            next_format_idx = (next_format_idx + 1) % #formats + 1
        elseif op == 'create index' then
            box.space.test:create_index('sk_' .. tostring(MAX_INDEX))
            MAX_INDEX = MAX_INDEX + 1
        elseif op == 'alter index' then
            box.space.test.index.sk_1:alter(
                {parts = {{field = 1, type = 'unsigned'}}})
        elseif op == 'drop index' then
            if MIN_INDEX > MAX_INDEX then
                -- No indexes left for drop - do another op
                do_random_ddl()
                return
            end
            box.space.test.index['sk_' .. tostring(MIN_INDEX)]:drop()
            MIN_INDEX = MIN_INDEX + 1
        end
    end

    local function do_random_dml()
        local idx = math.random(#dml_ops)
        local op = dml_ops[idx]
        if op == 'insert' then
            box.space.test:insert{MAX_TUPLE + 1}
            MAX_TUPLE = MAX_TUPLE + 1
        elseif op == 'replace' then
            local value = math.random(MAX_TUPLE)
            box.space.test:replace{value}
        elseif op == 'delete' then
            local value = math.random(MAX_TUPLE)
            box.space.test:delete{value}
        elseif op == 'get' then
            local value = math.random(MAX_TUPLE)
            box.space.test:get{value}
        end
    end

    -- Number of iterations
    local N = 200

    local function dml_fiber_f()
        for _ = 1, N do
            pcall(function()
                local ops_num = math.random(max_dml_ops)
                box.atomic(function()
                    for _ = 1, ops_num do
                        do_random_dml()
                        if interactive_dml and math.random(1, 10) == 10 then
                            -- Yield with 10% probability
                            fiber.sleep(0)
                        end
                    end
                end)
                successfully_completed_dml = successfully_completed_dml + 1
            end)
        end
    end

    local function ddl_fiber_f()
        for _ = 1, N do
            pcall(function()
                local ops_num = math.random(max_ddl_ops)
                box.atomic(function()
                    for _ = 1, ops_num do
                        do_random_ddl()
                    end
                end)
                successfully_completed_ddl = successfully_completed_ddl + 1
            end)
        end
    end

    local DML_FIBERS_MAX = 100
    local DDL_FIBERS_MAX = 3

    local dml_fibers_num = math.random(DML_FIBERS_MAX)
    local ddl_fibers_num = math.random(DDL_FIBERS_MAX)

    local fibers = {}
    for _ = 1, dml_fibers_num do
        local f = fiber.new(dml_fiber_f)
        f:set_joinable(true)
        table.insert(fibers, f)
    end
    for _ = 1, ddl_fibers_num do
        local f = fiber.new(ddl_fiber_f)
        f:set_joinable(true)
        table.insert(fibers, f)
    end

    for _, f in pairs(fibers) do
        local ok, err = f:join()
        t.assert(ok)
    end

    -- Check if at least one transaction of each type was successful
    t.assert_ge(successfully_completed_ddl, 1)
    t.assert_ge(successfully_completed_dml, 1)
end

g.test_stress = function(cg)
    if cg.params.disable_background_index_build then
        t.tarantool.skip_if_not_debug()
        cg.server:exec(function()
            box.error.injection.set('ERRINJ_BUILD_INDEX_DISABLE_YIELD', true)
        end)
    end

    local seed = os.time()
    print('Running stress test with seed: ' .. seed)
    cg.server:exec(stress_test_impl, {seed, cg.params.is_interactive})
end
