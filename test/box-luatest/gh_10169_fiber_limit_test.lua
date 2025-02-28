local t = require('luatest')
local server = require('luatest.server')

local g = t.group('fiber_limit_test')

g.before_all(function()
    g.server = server:new({
        alias = 'limited_instance',
        box_cfg = { fiber_limit = 30 }
    })
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
    g.server:drop()
end)

g.test_fiber_limit_exceeded = function()
    local fiber_limit = 30

    local result = g.server:exec(function(fiber_limit)
        local fiber = require('fiber')
        local errors = {}

        for _ = 1, fiber_limit + 5 do
            local ok, err = pcall(fiber.new, function() fiber.sleep(10) end)
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

    local system_fiber_ok = g.server:exec(function()
        for i = 1, 10 do
            local space_name = "test_space_" .. i
            local s = box.schema.space.create(space_name, { if_not_exists = true })
            s:create_index('pk', { parts = { {1, 'unsigned'} }, if_not_exists = true })
            s:insert({i})
        end
        return true
    end)

    t.assert(system_fiber_ok)
end
