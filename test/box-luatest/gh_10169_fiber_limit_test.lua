local t = require('luatest')
local server = require('luatest.server')
local netbox = require('net.box')

local g = t.group('fiber_limit_test')

g.before_all(function()
    g.server = server:new({
        alias = 'limited_instance',
        box_cfg = {}
    })
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
    g.server:drop()
end)

g.test_fiber_limit_and_system_fibers = function()
    local fiber_limit = 30

    local result = g.server:exec(function(fiber_limit)
        local fiber = require('fiber')
        fiber.client_fiber_limit(fiber_limit)
        local errors = {}

        for _ = 1, fiber_limit + 5 do
            local ok, err = pcall(fiber.new, function() fiber.sleep(100) end)
            if not ok then
                table.insert(errors, tostring(err))
                break
            end
        end

        return { errors = errors }
    end, {fiber_limit})

    local errors = result.errors
    t.assert_gt(#errors, 0)
    t.assert_str_contains(errors[1], "fiber count is exceeded")

    local conns = {}
    for i = 1, 10 do
        conns[i] = netbox.connect(g.server.net_box_uri)
        t.assert(conns[i]:ping())
    end

    local sys_fiber_ok = g.server:exec(function()
        return true
    end)
    t.assert(
        sys_fiber_ok,
        "System fibers should work, even if the client limit is exceeded"
    )

    for i = 1, 10 do
        conns[i]:close()
    end
end
