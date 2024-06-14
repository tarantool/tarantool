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

        -- Memtx TREE index supports the fast_offset option.
        s:create_index('pk', {type = 'TREE', fast_offset = true})

        -- Memtx HASH index does not support the fast_offset option.
        t.assert_error_msg_content_equals(
            "Can't create or modify index 'sk' in space 'test': " ..
            "HASH index does not support logarithmic select with offset",
            s.create_index, s, 'sk', {type = 'HASH', fast_offset = true})

        -- Memtx RTREE index does not support the fast_offset option.
        t.assert_error_msg_content_equals(
            "Can't create or modify index 'sk' in space 'test': " ..
            "RTREE index does not support logarithmic select with offset",
            s.create_index, s, 'sk', {type = 'RTREE', fast_offset = true,
                                      unique = false})

        -- Memtx BITSET index does not support the fast_offset option.
        t.assert_error_msg_content_equals(
            "Can't create or modify index 'sk' in space 'test': " ..
            "BITSET index does not support logarithmic select with offset",
            s.create_index, s, 'sk', {type = 'BITSET', fast_offset = true,
                                      unique = false})

        -- Can successfully create all indexes with fast_offset = false.
        s:create_index('k0', {type = 'TREE', fast_offset = false})
        s:create_index('k1', {type = 'HASH', fast_offset = false})
        s:create_index('k2', {type = 'RTREE', fast_offset = false,
                              unique = false, parts = {2, 'array'}})
        s:create_index('k3', {type = 'BITSET', fast_offset = false,
                              unique = false})

        -- The indexes have the fast_offset expected.
        t.assert_equals(s.index.pk.fast_offset, true)
        t.assert_equals(s.index.k0.fast_offset, false)
        t.assert_equals(s.index.k1.fast_offset, nil)
        t.assert_equals(s.index.k2.fast_offset, nil)
        t.assert_equals(s.index.k3.fast_offset, nil)

        s:drop()

        s = box.schema.space.create('test', {engine = 'vinyl'})

        -- Vinyl does not support the fast_offset option.
        t.assert_error_msg_content_equals(
            "Vinyl does not support logarithmic select with offset",
            s.create_index, s, 'pk', {fast_offset = true})

        -- Can successfully create vinyl index with fast_offset = false.
        s:create_index('pk', {fast_offset = false})

        -- The index has the fast_offset expected.
        t.assert_equals(s.index.pk.fast_offset, nil)

        s:drop()
    end)
end
