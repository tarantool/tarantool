local t = require('luatest')
local server = require('luatest.server')

-- 1. Many small xlogs. Let's check that threshold check indeed use
--    the sum of xlog sizes.
-- 2. One big xlog. Let's check that xlog right after the snapshot is
--    taken into account.
local group_config = {{wal_max_size = 50000}, {wal_max_size = 50000 * 10}}
local g = t.group("wal_max_size", group_config)

g.before_each(function(cg)
    cg.server = server:new({box_cfg = {
        wal_max_size = cg.params.wal_max_size,
        checkpoint_wal_threshold = 10 * 50000,
        checkpoint_count = 1e8,
        checkpoint_interval = 0,
    }})
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {sequence = true})
        -- Let's delete all existing xlogs so ones that were produced during
        -- server start and index creation don't interfere with our test and
        -- we don't have to take the sizes of that xlogs into account.
        -- 1. Reconfigure so each new checkpoint results in GC trigger and
        --    deletion of all logs before the checkpoint.
        box.cfg{checkpoint_count = 1}
        -- 2. Delete all unintended xlogs.
        box.snapshot()
        -- 3. Effectively disable GC again.
        box.cfg{checkpoint_count = 1e8}
    end)
end)

g.test_box_checkpoint_wal_threshold_after_restart = function(cg)
    -- 1. Let's create some xlogs followed by a snapshot.
    cg.server:exec(function()
        local fiber = require("fiber")
        local s = box.space.test
        local checkpoints_before = #box.info.gc().checkpoints
        for _ = 1, 7000 do
            s:insert({box.NULL})
        end
        -- Wait for possible (but unwanted) checkpoint finish.
        fiber.sleep(1)
        t.assert_equals(#box.info.gc().checkpoints, checkpoints_before,
                        "Wrong batch size. Change the number of tuples")
        box.snapshot()
    end)
    cg.server:restart()
    -- 2. There are xlogs created before the snapshot. Check that they are
    --    ignored when calculating the threshold.
    cg.server:exec(function()
        local fiber = require("fiber")
        local s = box.space.test
        local checkpoints_before = #box.info.gc().checkpoints
        for _ = 1, 7000 do
            s:insert({box.NULL})
        end
        -- Result must be the same -- no new checkpoint.
        fiber.sleep(1)
        t.assert_equals(#box.info.gc().checkpoints, checkpoints_before,
                        "old xlogs may be used")
    end)
    cg.server:restart()
    -- 3. We expect a checkpoint after the batch of tuples below since again
    --    we have some xlogs before the restart but this time there were no
    --    new snapshots.
    cg.server:exec(function()
        local fiber = require("fiber")
        local checkpoints_before = #box.info.gc().checkpoints
        local s = box.space.test
        -- A checkpoint must be created after that batch.
        for _ = 1, 7000 do
            s:insert({box.NULL})
        end
        fiber.sleep(1)
        t.assert_gt(#box.info.gc().checkpoints, checkpoints_before)
    end)
end

g.after_each(function(cg)
    cg.server:drop()
end)
