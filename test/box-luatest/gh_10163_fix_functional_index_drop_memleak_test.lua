local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {
        memtx_memory = 128 * 1024 * 1024
    }})
    cg.server:start()
end)

g.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

-- Test memory allocated for functional index key is freed.
-- Otherwise we should fail to insert tuple.
g.test_functional_index_drop_memory = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local func_code = [[
        function(tuple)
            return {string.rep('a', 512 * 1024)}
        end]]
        box.schema.func.create('func', {
            body = func_code, is_deterministic = true, is_sandboxed = true})

        for c = 1,10 do -- luacheck: no unused
            local test = box.schema.space.create('test')
            test:create_index('pk')
            test:create_index('fk', {
                unique = false, func = 'func', parts = {{1, 'string'}}})
            for i = 1,64 do
                test:replace({i})
            end
            test:drop()
            fiber.yield()
            collectgarbage()
        end
    end)
end
