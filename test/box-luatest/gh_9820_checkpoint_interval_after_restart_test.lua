local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

local default_interval = 5
local new_interval = 4

g.before_all(function(g)
    g.server = server:new({box_cfg = {
        checkpoint_interval = default_interval,
        checkpoint_count = 3,
    }})
    g.server:start()
    g.server:exec(function()
        local s = box.schema.space.create('test', {
            format={{ 'id', type='unsigned', is_nullable=false }}
        })
        s:create_index('primary', { parts = { 'id' } })
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

local function test_template(server,
                             restart_after_snapshot,
                             reconfigure_interval)
    local interval = default_interval
    if reconfigure_interval then
        interval = new_interval
    end
    local signature = server:exec(function(interval)
        local fiber = require('fiber')
        local s = box.space.test
        s:insert{1}
        box.snapshot()
        local max_signature = 0
        for _, chpt in ipairs(box.info.gc().checkpoints) do
            if chpt.signature > max_signature then
                max_signature = chpt.signature
            end
        end
        s:insert{2}
        fiber.sleep(interval)
        return max_signature
    end, {interval})
    if restart_after_snapshot then
        server:restart()
    end
    server:exec(function(signature, interval, reconfigure_interval)
        local fiber = require('fiber')
        box.space.test:insert{3}
        if reconfigure_interval then
            box.cfg{checkpoint_interval=interval}
        end
        fiber.sleep(interval + 1)
        local was_snapshot_after = false
        for _, chpt in ipairs(box.info.gc().checkpoints) do
            if chpt.signature > signature then
                was_snapshot_after = true
            end
        end
        t.assert(was_snapshot_after)
        end, {signature, interval, reconfigure_interval}
    )
end

g.test_box_checkpoint_after_restart = function(g)
    test_template(g.server, true, false)
end

g.test_box_checkpoint_after_reconfigure = function(g)
    test_template(g.server, false, true)
end
