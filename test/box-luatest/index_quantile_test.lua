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
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        box.schema.user.drop('test', {if_exists = true})
        box.schema.func.drop('test', {if_exists = true})
    end)
end)

g.test_arg_check = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local i = s:create_index('primary')
        local function check_err(msg, ...)
            t.assert_error_covers({
                type = 'IllegalParams',
                message = msg,
            }, ...)
        end
        check_err('Use space:quantile(...) instead of space.quantile(...)',
                  s.quantile)
        check_err('Use index:quantile(...) instead of index.quantile(...)',
                  i.quantile)
        check_err('Usage: index:quantile(level[, begin_key, end_key])',
                  i.quantile, i)
        check_err('level must be a number',
                  i.quantile, i, 'foo')
        check_err('level must be > 0 and < 1',
                  i.quantile, i, 0)
        check_err('level must be > 0 and < 1',
                  i.quantile, i, 1)
        check_err('level must be > 0 and < 1',
                  i.quantile, i, 1.5)
    end)
end

g.test_range_check = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local i = s:create_index('primary', {
            parts = {
                {1, 'unsigned'},
                {2, 'unsigned'},
            }
        })

        local key = function() end
        local err = {
            type = 'ClientError',
            name = 'PROC_LUA',
            message = "can not encode Lua type: 'function'",
        }
        t.assert_error_covers(err, i.quantile, i, 0.5, key)
        t.assert_error_covers(err, i.quantile, i, 0.5, nil, key)

        key = {'foo', 'bar'}
        err = {
            type = 'ClientError',
            name = 'KEY_PART_TYPE',
            message = 'Supplied key type of part 0 does not match ' ..
                      'index part type: expected unsigned',
        }
        t.assert_error_covers(err, i.quantile, i, 0.5, key)
        t.assert_error_covers(err, i.quantile, i, 0.5, nil, key)

        key = {1, 2, 3}
        err = {
            type = 'ClientError',
            name = 'KEY_PART_COUNT',
            message = 'Invalid key part count (expected [0..2], got 3)',
        }
        t.assert_error_covers(err, i.quantile, i, 0.5, key)
        t.assert_error_covers(err, i.quantile, i, 0.5, nil, key)

        err = {
            type = 'IllegalParams',
            message = 'begin_key must be < end_key',
        }
        t.assert_error_covers(err, i.quantile, i, 0.5, {10}, {5})
        t.assert_error_covers(err, i.quantile, i, 0.5, {10}, {10})
        t.assert_error_covers(err, i.quantile, i, 0.5, {10, 10}, {10})
        t.assert_error_covers(err, i.quantile, i, 0.5, {10, 10}, {10, 5})
        t.assert_error_covers(err, i.quantile, i, 0.5, {10, 10}, {10, 10})
    end)
end

g.test_index_check = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_INDEX_ID',
        }, s.quantile, s, 0.5)

        local i = s:create_index('primary')
        box.schema.user.create('test')
        t.assert_error_covers({
            type = 'AccessDeniedError',
            user = 'test',
            object_type = 'space',
            object_name = 'test',
        }, box.session.su, 'test', i.quantile, i, 0.5)

        i:drop()
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_INDEX_ID',
        }, i.quantile, i, 0.5)

        s:drop()
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_SPACE',
        }, i.quantile, i, 0.5)
    end)
end

g.test_unsupported = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local i1 = s:create_index('i1', {
            type = 'hash',
        })
        local i2 = s:create_index('i2', {
            type = 'rtree', parts = {2, 'array'},
        })
        local i3 = s:create_index('i3', {
            type = 'bitset', parts = {3, 'unsigned'},
        })
        t.assert_error_covers({
            type = 'ClientError',
            message = "Index 'i1' (HASH) of space 'test' (memtx) " ..
                      "does not support quantile()",
        }, i1.quantile, i1, 0.5)
        t.assert_error_covers({
            type = 'ClientError',
            message = "Index 'i2' (RTREE) of space 'test' (memtx) " ..
                      "does not support quantile()",
        }, i2.quantile, i2, 0.5)
        t.assert_error_covers({
            type = 'ClientError',
            message = "Index 'i3' (BITSET) of space 'test' (memtx) " ..
                      "does not support quantile()",
        }, i3.quantile, i3, 0.5)
    end)
end

g.test_tree_index = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('primary', {
            parts = {
                {1, 'unsigned'},
                {3, 'unsigned'}
            },
        })
        t.assert_equals(s:quantile(0.5), nil)
        t.assert_equals(s:quantile(0.5, {}, {10}), nil)
        t.assert_equals(s:quantile(0.5, {10}, {}), nil)
        t.assert_equals(s:quantile(0.5, {10, 1}, {20, 2}), nil)

        s:insert({10, 'x', 30, 'a'})
        t.assert_equals(s:quantile(0.5), {10, 30})
        t.assert_equals(s:quantile(0.5, {}, {10}), nil)
        t.assert_equals(s:quantile(0.5, {}, {10, 30}), nil)
        t.assert_equals(s:quantile(0.5, {10}, {}), {10, 30})

        s:insert({10, 'y', 10, 'b'})
        s:insert({10, 'z', 20, 'c'})
        t.assert_equals(s:quantile(0.5), {10, 20})
        t.assert_equals(s:quantile(0.5, {}, {10}), nil)
        t.assert_equals(s:quantile(0.5, {}, {10, 20}), {10, 10})
        t.assert_equals(s:quantile(0.5, {10}, {}), {10, 20})

        s:insert({20, box.NULL, 20})
        s:insert({30, box.NULL, 30})
        s:insert({40, box.NULL, 40})

        t.assert_equals(s:quantile(0.1), {10, 10})
        t.assert_equals(s:quantile(0.5), {20, 20})
        t.assert_equals(s:quantile(0.9), {40, 40})

        t.assert_equals(s:quantile(0.5, {10}, {30}), {10, 30})
        t.assert_equals(s:quantile(0.5, {10, 100}, {50}), {30, 30})
    end)
end

g.test_multikey_index = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('primary')
        local i = s:create_index('multikey', {
            unique = false,
            parts = {{'[2][*]', 'string'}},
        })

        -- 1a 3b 5c 4d 2e
        s:insert({1, {'a', 'b', 'c'}})
        s:insert({2, {'b', 'b', 'c', 'c', 'd', 'd'}})
        s:insert({3, {'c', 'd', 'e'}})
        s:insert({4, {'c', 'd', 'e'}})

        t.assert_equals(i:quantile(0.01), {'a'})
        t.assert_equals(i:quantile(0.10), {'b'})
        t.assert_equals(i:quantile(0.50), {'c'})
        t.assert_equals(i:quantile(0.80), {'d'})
        t.assert_equals(i:quantile(0.90), {'e'})

        t.assert_equals(i:quantile(0.5, {}, {'c'}), {'b'})
        t.assert_equals(i:quantile(0.5, {'c'}, {}), {'d'})
        t.assert_equals(i:quantile(0.5, {'b'}, {'e'}), {'c'})

        t.assert_equals(i:quantile(0.5, {}, {'a'}), nil)
        t.assert_equals(i:quantile(0.5, {'ff'}, {}), nil)
        t.assert_equals(i:quantile(0.5, {'aa'}, {'ab'}), nil)
    end)
end

g.test_func_index = function(cg)
    cg.server:exec(function()
        box.schema.func.create('test', {
            is_deterministic = true,
            is_multikey = true,
            is_sandboxed = true,
            body = [[function(tuple)
                return {{tuple[2]}, {tuple[3]}}
            end]]
        })
        local s = box.schema.space.create('test')
        s:create_index('primary')
        local i = s:create_index('func', {
            unique = false,
            func = 'test',
            parts = {{1, 'string'}}
        })

        -- 2a 2b 2c 2d 2e
        s:insert({1, 'a', 'b'})
        s:insert({2, 'b', 'c'})
        s:insert({3, 'c', 'd'})
        s:insert({4, 'd', 'e'})
        s:insert({5, 'e', 'a'})

        t.assert_equals(i:quantile(0.1), {'a'})
        t.assert_equals(i:quantile(0.3), {'b'})
        t.assert_equals(i:quantile(0.5), {'c'})
        t.assert_equals(i:quantile(0.7), {'d'})
        t.assert_equals(i:quantile(0.9), {'e'})

        t.assert_equals(i:quantile(0.5, {}, {'d'}), {'b'})
        t.assert_equals(i:quantile(0.5, {'c'}, {}), {'d'})
        t.assert_equals(i:quantile(0.5, {'b'}, {'e'}), {'c'})

        t.assert_equals(i:quantile(0.5, {}, {'a'}), nil)
        t.assert_equals(i:quantile(0.5, {'ff'}, {}), nil)
        t.assert_equals(i:quantile(0.5, {'aa'}, {'ab'}), nil)
    end)
end
