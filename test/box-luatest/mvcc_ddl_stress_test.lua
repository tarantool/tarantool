local server = require('luatest.server')
local t = require('luatest')

local g = t.group('Stress tests for MVCC DDL', t.helpers.matrix{
    is_interactive = {true, false},
    disable_background_index_build = {true, false},
    errinj_wal = {true, false},
})

g.before_each(function(cg)
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:stop()
end)

local function stress_test_impl(seed, interactive_dml, wal_errinj)
    math.randomseed(seed)
    local fiber = require('fiber')
    local log = require('log')
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

    local max_ddl_ops = 3
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
    local N = 50

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

    if wal_errinj then
        local function wal_errinj_fiber_f()
            for _ = 1, 5 do
                -- Inject each error with 0.8 probability so that probability
                -- of both errors being injected is more than 0.5
                if math.random(1, 10) < 9 then
                    box.error.injection.set('ERRINJ_WAL_DELAY', true)
                    fiber.sleep(0.5)
                    box.error.injection.set('ERRINJ_WAL_DELAY', false)
                end
                if math.random(1, 10) < 9 then
                    box.error.injection.set('ERRINJ_WAL_ERROR', true)
                    fiber.sleep(0.5)
                    box.error.injection.set('ERRINJ_WAL_ERROR', false)
                end
            end
        end
        local f = fiber.new(wal_errinj_fiber_f)
        table.insert(fibers, f)
    end

    for _, f in pairs(fibers) do
        local ok = f:join()
        t.assert(ok)
    end

    -- Verbosity
    log.info('Successfully completed DML: ' .. successfully_completed_dml)
    log.info('Successfully completed DDL: ' .. successfully_completed_ddl)
end

g.test_stress = function(cg)
    if cg.params.disable_background_index_build then
        t.tarantool.skip_if_not_debug()
        cg.server:exec(function()
            box.error.injection.set('ERRINJ_BUILD_INDEX_DISABLE_YIELD', true)
        end)
    end

    if cg.params.wal_errinj then
        t.tarantool.skip_if_not_debug()
    end

    local seed = os.time()
    -- Print the seed so that the test can be easily reproduced at least on
    -- the same device
    print('Running stress test with seed: ' .. seed)
    cg.server:exec(stress_test_impl,
        {seed, cg.params.is_interactive, cg.params.wal_errinj})
end
