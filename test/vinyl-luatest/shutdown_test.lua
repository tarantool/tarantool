local server = require('luatest.server')
local fiber = require('fiber')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

local function test_no_hang_on_shutdown(server)
    local channel = fiber.channel()
    fiber.create(function()
        server:stop()
        channel:put('finished')
    end)
    t.assert(channel:get(60) ~= nil)
end

-- Test we interrupt wait on vinyl dump caused by index creation.
-- Case 1. No snapshot is in progress.
g.test_shutdown_vinyl_dump = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        box.schema.create_space('test', {engine = 'vinyl'})
        box.space.test:create_index('pk')
        fiber.set_slice(100)
        box.begin()
        for i=1,10000 do
            box.space.test:insert{i, i}
        end
        box.commit()
        box.error.injection.set('ERRINJ_VY_RUN_WRITE_STMT_TIMEOUT', 0.01)
        fiber.new(function()
            box.space.test:create_index('sk', {parts = {2}})
        end)
    end)
    -- There are other yields before dump on vinyl index creation.
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('dump started'))
    end)
    test_no_hang_on_shutdown(cg.server)
end

-- Test we interrupt wait on vinyl dump caused by index creation.
-- Case 2. Snapshot is in progress.
g.test_shutdown_vinyl_dump_during_snapshot = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        box.schema.create_space('test', {engine = 'vinyl'})
        box.space.test:create_index('pk')
        fiber.set_slice(100)
        box.begin()
        for i=1,10000 do
            box.space.test:insert{i, i}
        end
        box.commit()
        box.error.injection.set('ERRINJ_VY_RUN_WRITE_STMT_TIMEOUT', 0.01)
        fiber.new(function()
            box.snapshot()
        end)
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log('vinyl checkpoint started'))
    end)
    cg.server:exec(function()
        local fiber = require('fiber')
        fiber.new(function()
            box.space.test:create_index('sk', {parts = {2}})
        end)
    end)
    -- Sleep to pass building index and reach sleep in dump on waiting
    -- checkpoint to finish. If we fail to pass index code we will
    -- be able to pass the test as index building loop is cancellable.
    -- Yet this test is not aimed to test the cancellability of index
    -- building loop.
    fiber.sleep(3)
    test_no_hang_on_shutdown(cg.server)
end
