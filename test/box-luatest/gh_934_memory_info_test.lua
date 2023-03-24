local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_memtx_index = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('i1')
        s:create_index('i2', {type = 'hash', parts = {2, 'unsigned'}})
        s:create_index('i3', {unique = false, type = 'bitset',
                              parts = {3, 'unsigned'}})
        s:create_index('i4', {unique = false, type = 'rtree',
                              parts = {4, 'array'}})
        local stat1 = box.info.memory()
        s:insert({1, 1, 1, {1, 1}})
        local stat2 = box.info.memory()
        -- The first insertion triggers allocation of 3 extents of size 16384
        -- bytes per each index.
        t.assert_equals(stat2.index - stat1.index, 3 * 16384 * (#s.index + 1))
    end)
end

g.test_memtx_data = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        local tuple1 = box.tuple.new({1, string.rep('x', 100)})
        local tuple2 = box.tuple.new({2, string.rep('x', 101)})
        collectgarbage('collect') -- drop Lua refs
        local stat1 = box.info.memory()
        s:insert(tuple1)
        local stat2 = box.info.memory()
        local tuple_overhead = stat2.data - stat1.data - tuple1:bsize()
        t.assert_gt(tuple_overhead, 0)
        s:insert(tuple2)
        local stat3 = box.info.memory()
        t.assert_equals(stat3.data - stat2.data,
                        tuple_overhead + tuple2:bsize())
        s:delete(tuple1[1])
        s:delete(tuple2[1])
        box.tuple.new() -- drop blessed tuple ref
        collectgarbage('collect') -- drop Lua refs
        local stat4 = box.info.memory()
        t.assert_equals(stat4.data, stat1.data)
    end)
end
