local t = require('luatest')
local server = require('test.luatest_helpers.server')

local g = t.group('gh-4264')

g.before_all(function(cg)
    cg.server = server:new{alias = 'server'}
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_recursive_trigger_invocation = function(cg)
    local order = cg.server:exec(function()
        local order = {}
        local level = 0
        local s = box.space.test
        local f1 = function()
            level = level + 1
            table.insert(order, level * 10 + 1)
            if level >= 3 then
                return
            end
            s:replace{1}
            level = level - 1
        end
        local f2 = function()
            table.insert(order, level * 10 + 2)
        end
        s:on_replace(f2)
        s:on_replace(f1)
        s:replace{1}
        return order
    end)
    t.assert_equals(order, {11, 21, 31, 32, 22, 12},
                    "Correct recursive trigger invocation")
end
