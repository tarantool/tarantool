local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-13410', {
    {index_opts = {parts = {{field = 2, path = '[*]'}}, unique = false}},
    {index_opts = {parts = {{1, 'unsigned'}}, func = 'f', unique = false}},
})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        box.schema.func.create('f', {
            body = [[
                function(tuple)
                    local keys = {}
                    for _, k in ipairs(tuple[2]) do
                        table.insert(keys, {k})
                    end
                    return keys
                end
            ]],
            is_deterministic = true,
            is_sandboxed = true,
            opts = {is_multikey = true},
        })
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function(index_opts)
        local s = box.schema.create_space('s')
        s:create_index('pk')
        s:create_index('mk', index_opts)
    end, {cg.params.index_opts})
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

-- Check that an iterator paused on an entry of a tuple with several index
-- entries continues from that entry after the tree has been modified: another
-- entry of the same tuple must not be taken for the current one.
g.test_iterator_position_after_insert = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.space.s
        s:replace{1, {3, 4, 5}}
        local res = {}
        local ch = fiber.channel(1)
        local f = fiber.new(function()
            local gen, param, state = s.index.mk:pairs({}, {iterator = 'GE'})
            local tuple
            for _ = 1, 2 do
                state, tuple = gen(param, state)
                table.insert(res, tuple)
            end
            ch:get()
            while true do
                state, tuple = gen(param, state)
                if state == nil then
                    break
                end
                table.insert(res, tuple)
            end
            ch:put(true)
        end)
        f:set_joinable(true)
        fiber.sleep(0)
        -- The new entry is stored before the entries of tuple 1.
        s:replace{2, {2}}
        ch:put(true)
        ch:get()
        f:join()
        t.assert_equals(res, {{1, {3, 4, 5}}, {1, {3, 4, 5}}, {1, {3, 4, 5}}})
    end)
end
