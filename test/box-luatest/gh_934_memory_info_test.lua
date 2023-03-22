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
        s:drop()
    end)
end
