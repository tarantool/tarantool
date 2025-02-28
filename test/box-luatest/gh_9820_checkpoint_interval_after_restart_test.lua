local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(g)
    g.server = server:new({box_cfg = {
        checkpoint_interval = 25,
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
        box.cfg{checkpoint_interval=25}
    end)
end)

g.test_box_checkpoint_after_restart = function(g)
    local checkpoint_count = g.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        s:insert({box.NULL})
        box.snapshot()
        local checkpoint_count = #box.info.gc().checkpoints
        s:insert({box.NULL})
        fiber.sleep(25)
        return checkpoint_count
    end)
    g.server:restart()
    g.server:exec(function(checkpoint_count)
        local fiber = require('fiber')
        fiber.sleep(28)
        t.assert_gt(#box.info.gc().checkpoints, checkpoint_count)
    end, {checkpoint_count})
end

g.test_box_checkpoint_after_reconfigure = function(g)
    g.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.test
        s:insert({box.NULL})
        box.snapshot()
        local checkpoint_count = #box.info.gc().checkpoints
        s:insert({box.NULL})
        fiber.sleep(7)
        box.cfg{checkpoint_interval=7}
        fiber.sleep(12)
        t.assert_gt(#box.info.gc().checkpoints, checkpoint_count)
    end)
end

g.after_all(function(g)
    g.server:drop()
end)
