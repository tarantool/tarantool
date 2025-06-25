local t = require('luatest')
local server = require('luatest.server')

local g_generic = t.group('sequential_index_build_test-generic', {
    {memtx_use_mvcc_engine = false},
    {memtx_use_mvcc_engine = true},
})
local g_cord_yield = t.group('sequential_index_build_test-cord-yield')

for _, g in pairs({g_generic, g_cord_yield}) do
    g.before_each(function(cg)
        cg.server = server:new({alias = 'master'})
        cg.server:start()
    end)

    g.after_each(function(cg)
        cg.server:drop()
    end)
end

-- Create a space with data and indexes.
local function create_space(cg, name, create_sks, tuple_count)
    cg.server:exec(function(name, create_sks, tuple_count)
        local s = box.schema.create_space(name)
        s:create_index('pk')
        if create_sks then
            s:create_index('sk1')
            s:create_index('sk2', {parts = {1, 'unsigned',
                                            sort_order = 'desc'}})
        end
        for i = 1, tuple_count do
            s:insert({i})
        end

        box.snapshot()
    end, {name, create_sks, tuple_count})
end

-- Check the created space and indexes.
local function check_space(cg, name)
    cg.server:exec(function(name)
        local fiber = require('fiber')
        local s = box.space[name]
        local i = 0

        -- pk is ascending.
        for _, tuple in s:pairs() do
            i = i + 1
            t.assert_equals(tuple[1], i)
            if i % 100 == 0 then
                fiber.yield()
            end
        end
        t.assert_equals(i, s.index.pk:len())

        -- Break if no secondary keys.
        if #s.index == 0 then
            return
        end

        -- Yields assume small tuple count.
        t.assert_equals(s.index.pk:len(), 100)

        -- sk2 is descending.
        for _, tuple in s.index.sk2:pairs() do
            t.assert_equals(tuple[1], i)
            i = i - 1
        end
        t.assert_equals(i, 0)
        fiber.yield()

        -- sk1 is ascending.
        for _, tuple in s.index.sk1:pairs() do
            i = i + 1
            t.assert_equals(tuple[1], i)
        end
        t.assert_equals(i, 100)
        fiber.yield()
    end, {name})
end

-- Create a snapshot.
local function create_snapshot(cg)
    cg.server:exec(function()
        box.snapshot()
    end)
end

-- Restart the instance using the new snapshot.
local function restart_server(cg)
    cg.server:restart({
        box_cfg = {memtx_use_mvcc_engine = cg.params.memtx_use_mvcc_engine},
    })
end

-- Test that snapshot with no user spaces loads successfully.
g_generic.test_no_spaces = function(cg)
    cg.server:exec(function()
        box.space._space:alter({}) -- No-op to update the VClock.
    end)
    create_snapshot(cg)

    restart_server(cg)
    t.assert(cg.server:grep_log('ready to accept requests'))
end

-- Test that a single user space is loaded successfully.
g_generic.test_single_space = function(cg)
    create_space(cg, 's', true, 100)
    create_snapshot(cg)

    restart_server(cg)
    check_space(cg, 's')
end

-- Test that multiple user spaces are loaded successfully.
g_generic.test_multiple_spaces = function(cg)
    create_space(cg, 's1', true, 100)
    create_space(cg, 's2', true, 100)
    create_space(cg, 's3', true, 100)
    create_snapshot(cg)

    restart_server(cg)
    check_space(cg, 's1')
    check_space(cg, 's2')
    check_space(cg, 's3')
end

-- Test that the fiber yield that can happen in the key sorting cord
-- does not break the recovery procedure. Let's slow-down the build
-- array check so that the `cord_cojoin` has to wait for it.
g_cord_yield.test_tt_sort_cord_yield = function(cg)
    t.tarantool.skip_if_not_debug()
    create_space(cg, 's1', false, 2000)
    create_space(cg, 's2', false, 2000)

    -- Successfully load the snapshot with slowed-down tt_sort stage.
    cg.server:restart({
        env = {['TARANTOOL_RUN_BEFORE_BOX_CFG'] =
                   'box.error.injection.set(' ..
                   '\'ERRINJ_TT_SORT_CHECK_PRESORTED_DELAY\', 0.2)'},
    })
    check_space(cg, 's1')
    check_space(cg, 's2')
end
