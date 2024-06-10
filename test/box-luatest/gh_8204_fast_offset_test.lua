local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.after_each(function()
    g.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

g.test_option = function()
    g.server:exec(function()
        local s = box.schema.space.create('test', {engine = 'memtx'})

        -- Memtx does not support the fast_offset option.
        t.assert_error_msg_content_equals(
            "memtx does not support logarithmic select with offset",
            s.create_index, s, 'pk', {fast_offset = true})

        -- Can successfully create all indexes with fast_offset = false.
        s:create_index('k0', {type = 'TREE', fast_offset = false})
        s:create_index('k1', {type = 'HASH', fast_offset = false})
        s:create_index('k2', {type = 'RTREE', fast_offset = false,
                              unique = false, parts = {2, 'array'}})
        s:create_index('k3', {type = 'BITSET', fast_offset = false,
                              unique = false})

        s:drop()

        s = box.schema.space.create('test', {engine = 'vinyl'})

        -- Vinyl does not support the fast_offset option.
        t.assert_error_msg_content_equals(
            "Vinyl does not support logarithmic select with offset",
            s.create_index, s, 'pk', {fast_offset = true})

        -- Can successfully create vinyl index with fast_offset = false.
        s:create_index('pk', {fast_offset = false})

        s:drop()
    end)
end
