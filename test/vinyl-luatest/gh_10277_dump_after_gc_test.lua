local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new{
        box_cfg = {
            checkpoint_count = 1,
        },
    }
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.backup.stop()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        box.error.injection.set('ERRINJ_VY_DUMP_DELAY', false)
    end)
end)

g.test_dump_after_gc = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        -- Pause garbage collection.
        box.backup.start()

        -- Create a space.
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('pk')

        -- Create a checkpoint.
        s:insert({1})
        box.snapshot()

        -- Start checkpointing in background.
        box.error.injection.set('ERRINJ_VY_DUMP_DELAY', true)
        s:insert({2})
        local f = fiber.new(box.snapshot)
        f:set_joinable(true)
        fiber.sleep(0.1)
        t.assert_equals(box.info.gc().checkpoint_is_in_progress, true)

        -- Drop the space.
        s:drop()

        -- Resume garbage collection and wait for it to complete.
        box.backup.stop()
        t.helpers.retrying({}, function()
            t.assert_equals(#box.info.gc().checkpoints, 1)
        end)

        -- Complete checkpointing.
        box.error.injection.set('ERRINJ_VY_DUMP_DELAY', false)
        t.assert_equals({f:join()}, {true, 'ok'})
    end)
end
