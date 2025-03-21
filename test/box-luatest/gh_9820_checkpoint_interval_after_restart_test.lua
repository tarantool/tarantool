local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(g)
    g.server = server:new({box_cfg = {
        checkpoint_interval = 5,
        checkpoint_count = 1e9,
    }})
    g.server:start()
    g.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {sequence = true})
    end)
end)

g.before_each(function(g)
    g.server:exec(function()
        box.space.test:truncate()
        box.cfg{checkpoint_interval=5}
    end)
end)

g.test_box_checkpoint_after_restart = function(g)
    local fiber = require('fiber')
    local checkpoint_count = g.server:exec(function()
        local s = box.space.test
        t.helpers.retrying({}, function()
            t.assert_not(box.info.gc().checkpoint_is_in_progress)
        end)
        s:insert({box.NULL})
        box.snapshot()
        local checkpoint_count = #box.info.gc().checkpoints
        s:insert({box.NULL})
        return checkpoint_count
    end)
    g.server:stop()
    fiber.sleep(5)
    g.server:start()
    g.server:exec(function(checkpoint_count)
        local fiber = require('fiber')
        fiber.sleep(7)
        t.assert_gt(#box.info.gc().checkpoints, checkpoint_count)
    end, {checkpoint_count})
end

g.test_box_checkpoint_after_reconfigure = function(g)
    g.server:exec(function()
        box.cfg{checkpoint_interval = 25}
        local fiber = require('fiber')
        local s = box.space.test
        t.helpers.retrying({}, function()
            t.assert_not(box.info.gc().checkpoint_is_in_progress)
        end)
        s:insert({box.NULL})
        box.snapshot()
        local checkpoint_count = #box.info.gc().checkpoints
        s:insert({box.NULL})
        fiber.sleep(5)
        box.cfg{checkpoint_interval = 5}
        fiber.sleep(7)
        t.assert_gt(#box.info.gc().checkpoints, checkpoint_count)
    end)
end

g.after_all(function(g)
    g.server:drop()
end)
