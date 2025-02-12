local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

local default_interval = 10
local checkpoint_count = 1e9
local new_interval = 4

g.before_all(function(g)
    g.server = server:new({box_cfg = {
        checkpoint_interval = default_interval,
        checkpoint_count = checkpoint_count,
    }})
    g.server:start()
    g.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', { sequence = true })
    end)
end)

g.after_all(function(g)
    g.server:drop()
end)

g.before_each(function(g)
    g.server:exec(function()
        box.space.test:truncate()
    end)
end)

g.test_box_checkpoint_after_restart = function(g)
    local checkpoint_count = g.server:exec(function(interval)
        local s = box.space.test
        s:insert({box.NULL})
        box.snapshot()
        local checkpoint_count = #box.info.gc().checkpoints
        s:insert({box.NULL})
        require('fiber').sleep(interval)
        return checkpoint_count
    end, {default_interval})
    g.server:exec(function(interval, checkpoint_count)
        box.space.test:insert({box.NULL})
        require('fiber').sleep(interval + 1)
        t.assert_gt(#box.info.gc().checkpoints, checkpoint_count)
    end, {default_interval, checkpoint_count})
end

g.test_box_checkpoint_after_reconfigure = function(g)
    g.server:exec(function(interval)
        local s = box.space.test
        s:insert({box.NULL})
        box.snapshot()
        local checkpoint_count = #box.info.gc().checkpoints
        s:insert({box.NULL})
        local fiber = require('fiber')
        fiber.sleep(interval)
        box.space.test:insert({box.NULL})
        box.cfg{checkpoint_interval=interval}
        fiber.sleep(interval + 1)
        t.assert_gt(#box.info.gc().checkpoints, checkpoint_count)
    end, {new_interval})
end
