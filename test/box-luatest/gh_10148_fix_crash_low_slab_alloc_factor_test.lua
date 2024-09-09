local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {
            slab_alloc_factor = 1.001,
            memtx_memory = 64 * 1024 * 1024,
            wal_mode = 'none',
        }
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_low_slab_alloc_factor = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        fiber.set_max_slice(30)
        local test = box.schema.create_space('test')
        test:create_index('pri')
        for i=1,1000000 do
            local ok, err = pcall(test.insert, test, {i, string.rep('x', 1000)})
            if not ok then
                t.assert_equals(err:unpack().type, 'OutOfMemory')
                break
            end
        end
    end)
end
