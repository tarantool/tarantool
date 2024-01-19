local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh_8530_alter_space_snapshot', {
    {memtx_use_mvcc_engine = true},
    {memtx_use_mvcc_engine = false},
})

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {
            memtx_use_mvcc_engine = cg.params.memtx_use_mvcc_engine,
        },
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('primary')
        s:insert({1, 10})
        s:insert({2, 20})
        s:insert({3, 30})
    end)
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:drop()
    end)
end)

-- Check that a new index record that hasn't been written to WAL doesn't make
-- it to a snapshot.
g.test_build_index = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        box.error.injection.set('ERRINJ_BUILD_INDEX_DELAY', true)
        local f = fiber.create(s.create_index, s, 'secondary',
                               {parts = {2, 'unsigned'}})
        fiber.sleep(0.01)
        box.snapshot()
        t.assert_equals(f:status(), 'suspended')
    end)
    -- Use KILL because server will hang on shutdown due to injection.
    -- We don't need graceful shutdown for the test anyway.
    cg.server.process:kill('KILL')
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.test
        t.assert_equals(s.index.secondary, nil)
    end)
end

-- Check that a space format record that hasn't been written to WAL doesn't
-- make it to a snapshot.
g.test_change_format = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        box.error.injection.set('ERRINJ_CHECK_FORMAT_DELAY', true)
        local f = fiber.create(s.format, s,
                               {{'a', 'unsigned'}, {'b', 'unsigned'}})
        fiber.sleep(0.01)
        box.snapshot()
        t.assert_equals(f:status(), 'suspended')
    end)
    -- Use KILL because server will hang on shutdown due to injection.
    -- We don't need graceful shutdown for the test anyway.
    cg.server.process:kill('KILL')
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.test
        t.assert_equals(s:format(), {})
    end)
end
