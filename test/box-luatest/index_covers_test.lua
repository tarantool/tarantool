local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(g)
    t.tarantool.skip_if_not_debug()
    g.server = server:new()
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.after_each(function(g)
    g.server:exec(function()
        box.space.test:drop()
    end)
end)

g.test_covers_validation = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {
            format = {{'a', 'unsigned'}, {'b', 'string'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: covers is allowed only for" ..
                      " secondary index"
        }, box.space._index.insert, box.space._index, {
            s.id, 0, 'pk', 'tree', {covers = {1}},
            {{0, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: covers is allowed only for" ..
                      " secondary index"
        }, s.create_index, s, 'pk', {covers = {2}})
        s:create_index('pk')
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: covers is allowed only for" ..
                      " secondary index"
        }, s.index.pk.alter, s.index.pk, {covers = {1}})
        --
        -- Test space:create_index.
        --
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options parameter 'covers' should be of type table",
        }, s.create_index, s, 'sk', {covers = 1})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.covers[1]: field (name or number) is expected",
        }, s.create_index, s, 'sk', {covers = {true}})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.covers[1]: field (number) must be one-based",
        }, s.create_index, s, 'sk', {covers = {0}})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.covers[1]: field was not found by name 'x'",
        }, s.create_index, s, 'sk', {covers = {'x'}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' has duplicates",
        }, s.create_index, s, 'sk', {covers = {1, 1}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' element 'field' value" ..
                      " must be unsigned",
        }, s.create_index, s, 'sk', {covers = {1.1}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' element 'field' value" ..
                      " must be unsigned",
        }, s.create_index, s, 'sk', {covers = {2^32 + 1}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' element must contain" ..
                      " at least a 'field' key",
        }, s.create_index, s, 'sk', {covers = {{}}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' element map key must" ..
                      " be string",
        }, s.create_index, s, 'sk', {covers = {{1, 'null_rle'}}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: unexpected 'covers' element map" ..
                      " key",
        }, s.create_index, s, 'sk', {covers = {{1, layout = 'null_rle',
                                                dummy_key = 'dummy'}}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' element 'layout' value" ..
                      " must be string",
        }, s.create_index, s, 'sk', {covers = {{1, layout = 2}}})
        --
        -- Test index:alter
        --
        local sk = s:create_index('sk')
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options parameter 'covers' should be of type table",
        }, sk.alter, sk, {covers = 1})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.covers[1]: field (name or number) is expected",
        }, sk.alter, sk, {covers = {true}})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.covers[1]: field (number) must be one-based",
        }, sk.alter, sk, {covers = {0}})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options.covers[1]: field was not found by name 'x'",
        }, sk.alter, sk, {covers = {'x'}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' has duplicates",
        }, sk.alter, sk, {covers = {1, 1}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' element 'field' value" ..
                      " must be unsigned",
        }, sk.alter, sk, {covers = {1.1}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' element 'field' value" ..
                      " must be unsigned",
        }, sk.alter, sk, {covers = {2^32 + 1}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' element must contain" ..
                      " at least a 'field' key",
        }, sk.alter, sk, {covers = {{}}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' element map key must" ..
                      " be string",
        }, sk.alter, sk, {covers = {{1, 'null_rle'}}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: unexpected 'covers' element map" ..
                      " key",
        }, sk.alter, sk, {covers = {{1, layout = 'null_rle',
                                     dummy_key = 'dummy'}}})
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' element 'layout' value" ..
                      " must be string",
        }, sk.alter, sk, {covers = {{1, layout = 2}}})
        s.index.sk:drop()
        --
        -- Test box.space._index:insert.
        --
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' must be array",
        }, box.space._index.insert, box.space._index, {
            s.id, 1, 'sk', 'tree', {unique = true, covers = 1},
            {{0, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' elements must be" ..
                      " unsigned or map",
        }, box.space._index.insert, box.space._index, {
            s.id, 1, 'sk', 'tree', {unique = true, covers = {true}},
            {{0, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' elements must be" ..
                      " unsigned or map",
        }, box.space._index.insert, box.space._index, {
            s.id, 1, 'sk', 'tree', {unique = true, covers = {-1}},
            {{0, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' elements must be" ..
                      " unsigned or map",
        }, box.space._index.insert, box.space._index, {
            s.id, 1, 'sk', 'tree', {unique = true, covers = {1.1}},
            {{0, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' elements must be" ..
                      " unsigned or map",
        }, box.space._index.insert, box.space._index, {
            s.id, 1, 'sk', 'tree', {unique = true, covers = {{1, 'null_rle'}}},
            {{0, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' elements must be" ..
                      " unsigned",
        }, box.space._index.insert, box.space._index, {
            s.id, 1, 'sk', 'tree', {unique = true, covers = {2^32}},
            {{0, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' has duplicates",
        }, box.space._index.insert, box.space._index, {
            s.id, 1, 'sk', 'tree', {unique = true, covers = {1,1}},
            {{0, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: unexpected 'covers' element map" ..
                      " key",
        }, box.space._index.insert, box.space._index, {
            s.id, 1, 'sk', 'tree', {
                unique = true, covers = {
                    {field = 1, layout = 'null_rle', dummy_key = 'dummy'},
                },
            }, {{0, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.WRONG_INDEX_OPTIONS,
            message = "Wrong index options: 'covers' element 'layout' value" ..
                      " must be string",
        }, box.space._index.insert, box.space._index, {
            s.id, 1, 'sk', 'tree', {
                unique = true, covers = {
                    {field = 1, layout = 2},
                },
            }, {{0, 'unsigned'}}
        })
    end)
end

g.test_covers_stubs = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test', {
            engine = 'memtx',
            format = {{'a', 'unsigned'}, {'b', 'string'}}
        })
        s:create_index('pk')
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.UNSUPPORTED,
            message = "memtx does not support covering index",
        }, s.create_index, s, 'sk', {covers = {2}})
        s:drop()
        local s = box.schema.create_space('test', {
            engine = 'vinyl',
            format = {{'a', 'unsigned'}, {'b', 'string'}}
        })
        s:create_index('pk')
        t.assert_error_covers({
            type = 'ClientError',
            code = box.error.UNSUPPORTED,
            message = "vinyl does not support covering index",
        }, s.create_index, s, 'sk', {covers = {2}})
    end)
end
