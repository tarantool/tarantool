local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new{
        box_cfg = {
            checkpoint_count = 1,
        },
    }
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_dump = function(cg)
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

        -- Make background checkpointing fail.
        box.error.injection.set('ERRINJ_VY_RUN_WRITE', true)
        box.error.injection.set('ERRINJ_VY_DUMP_DELAY', false)
        t.assert_error_msg_equals(
            "Error injection 'vinyl dump'",
            function()
                local ok, ret = f:join()
                if not ok then
                    error(ret)
                end
                return ret
            end)

        -- Try again to create a checkpoint.
        box.error.injection.set('ERRINJ_VY_RUN_WRITE', false)
        box.snapshot()
    end)
end

g.test_compaction = function(cg)
    cg.server:exec(function()
        -- Pause garbage collection.
        box.backup.start()

        -- Create a space.
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('pk')

        -- Start compaction in background.
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', true)
        s:insert({1})
        box.snapshot()
        s:insert({2})
        box.snapshot()
        s.index.pk:compact()
        t.helpers.retrying({}, function()
            t.assert_ge(box.stat.vinyl().scheduler.tasks_inprogress, 1)
        end)

        -- Drop the space.
        s:drop()

        -- Resume garbage collection and wait for it to complete.
        box.backup.stop()
        t.helpers.retrying({}, function()
            t.assert_equals(#box.info.gc().checkpoints, 1)
        end)

        -- Make background compaction fail.
        box.error.injection.set('ERRINJ_VY_RUN_WRITE', true)
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', false)
        t.helpers.retrying({}, function()
            t.assert_ge(box.stat.vinyl().scheduler.tasks_failed, 1)
        end)

        -- Try to create a checkpoint.
        box.error.injection.set('ERRINJ_VY_RUN_WRITE', false)
        box.snapshot()
    end)
end
