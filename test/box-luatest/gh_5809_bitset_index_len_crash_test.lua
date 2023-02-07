local t = require('luatest')
local server = require('luatest.server')

local g = t.group('')

g.before_all(function(cg)
    cg.server = server:new{alias = 'dflt'}
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks size of empty, i.e., uninitialized bitset index works correctly.
g.test_empty_uninit_bitset_index_len = function()
    g.server:exec(function()
        local t = require('luatest')

        local s = box.schema.space.create('s')
        s:create_index('pk')
        s:create_index('bitset', {type = 'bitset'})
        t.assert_equals(s.index.bitset:len(), 0)
    end)
end
