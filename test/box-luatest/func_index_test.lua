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
        -- Persistent functions are the ones having a Lua body.
        for _, f in box.space._func:pairs() do
            if f.body ~= '' then
                box.schema.func.drop(f.name)
            end
        end
    end)
end)

--
-- gh-1260: Func index.
--
g.test_func_index_ddl_errors = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local lua_code = 'function(tuple) return {tuple[1] + tuple[2]} end'
        local lua_code2 = [[
            function(tuple)
                return {tuple[1] + tuple[2], 2 * tuple[1] + tuple[2]}
            end
        ]]
        box.schema.func.create('s_nonpersistent')
        box.schema.func.create('s_nondeterministic', {
            body = lua_code, is_deterministic = false, is_sandboxed = true
        })
        box.schema.func.create('s', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        box.schema.func.create('ss', {
            body = lua_code2, is_deterministic = true, is_sandboxed = true
        })

        -- Func index can't be primary.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'MODIFY_INDEX',
            details = 'primary key can not use a function',
        }, s.create_index, s, 'idx', {
            func = box.func.s.id, parts = {{1, 'unsigned'}}
        })
        s:create_index('pk')
        -- Invalid fid.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_FUNCTION',
            func = '6666',
        }, s.create_index, s, 'idx', {
            func = 6666, parts = {{1, 'unsigned'}}
        })
        -- Can't use non-persistent function in functional index.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'WRONG_INDEX_OPTIONS',
            details = "function 's_nonpersistent' doesn't meet functional " ..
                      "index function criteria (stored, deterministic, " ..
                      "written in Lua)",
        }, s.create_index, s, 'idx', {
            func = box.func.s_nonpersistent.id, parts = {{1, 'unsigned'}}
        })
        -- Can't use non-deterministic function in functional index.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'WRONG_INDEX_OPTIONS',
            details = "function 's_nondeterministic' doesn't meet " ..
                      "functional index function criteria (stored, " ..
                      "deterministic, written in Lua)",
        }, s.create_index, s, 'idx', {
            func = box.func.s_nondeterministic.id, parts = {{1, 'unsigned'}}
        })
        -- Can't use non-sequential parts in returned key definition.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNC_INDEX_PARTS',
            message = 'Wrong functional index definition: key part ' ..
                      'numbers must be sequential and first part number ' ..
                      'must be 1',
        }, s.create_index, s, 'idx', {
            func = box.func.ss.id,
            parts = {{1, 'unsigned'}, {3, 'unsigned'}}
        })
        -- Can't use parts started not by 1 field.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNC_INDEX_PARTS',
            details = 'key part numbers must be sequential and first part ' ..
                      'number must be 1',
        }, s.create_index, s, 'idx', {
            func = box.func.ss.id,
            parts = {{2, 'unsigned'}, {3, 'unsigned'}}
        })
        -- Can't use JSON paths in returned key definition.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'UNSUPPORTED',
        }, s.create_index, s, 'idx', {
            func = box.func.ss.id, parts = {{"[1]data", 'unsigned'}}
        })
    end)
end

-- Can't drop a function referenced by functional index.
g.test_func_index_func_drop_references = function(cg)
    cg.server:exec(function()
        local lua_code = 'function(tuple) return {tuple[1] + tuple[2]} end'
        box.schema.func.create('s', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('idx', {
            unique = true, func = box.func.s.id, parts = {{1, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            name = 'DROP_FUNCTION',
            details = 'function has references',
        }, box.schema.func.drop, 's')
        box.snapshot()
    end)
    cg.server:restart()
    cg.server:exec(function()
        -- The reference survives the restart.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'DROP_FUNCTION',
            details = 'function has references',
        }, box.schema.func.drop, 's')
        box.space.test.index.idx:drop()
        box.schema.func.drop('s')
    end)
end

g.test_func_index_extractor_invalid_return = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')

        -- The extractor must return keys.
        local lua_code = 'function(tuple) return "hello" end'
        box.schema.func.create('invalidreturn0', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        local idx = s:create_index('idx', {
            func = box.func.invalidreturn0.id, parts = {{1, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNC_INDEX_FORMAT',
            index = 'idx',
            space = 'test',
            details = 'supplied key type is invalid: expected array',
        }, s.insert, s, {1})
        idx:drop()

        -- A string instead of unsigned.
        local lua_code = 'function(tuple) return {"hello"} end'
        box.schema.func.create('invalidreturn1', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        local idx = s:create_index('idx', {
            func = box.func.invalidreturn1.id, parts = {{1, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNC_INDEX_FORMAT',
            index = 'idx',
            space = 'test',
            details = 'key does not follow functional index definition',
        }, s.insert, s, {1})
        idx:drop()

        -- Invalid return format for multikey index.
        local lua_code = 'function(tuple) return {"hello", "world"}, {1, 2} end'
        box.schema.func.create('invalidreturn2', {
            body = lua_code, is_deterministic = true, is_sandboxed = true,
            opts = {is_multikey = true}
        })
        local idx = s:create_index('idx', {
            func = box.func.invalidreturn2.id,
            parts = {{1, 'unsigned'}, {2, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNC_INDEX_FORMAT',
            index = 'idx',
            space = 'test',
            details = 'to many values were returned',
        }, s.insert, s, {1})
        idx:drop()

        -- The second returned key is invalid.
        local lua_code = [[
            function(tuple)
                return {{"hello", "world"}, {1, 2}}
            end
        ]]
        box.schema.func.create('invalidreturn3', {
            body = lua_code, is_deterministic = true, is_sandboxed = true,
            opts = {is_multikey = true}
        })
        local idx = s:create_index('idx', {
            func = box.func.invalidreturn3.id,
            parts = {{1, 'unsigned'}, {2, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNC_INDEX_FORMAT',
            index = 'idx',
            space = 'test',
            details = 'key does not follow functional index definition',
        }, s.insert, s, {1})
        idx:drop()

        -- Multikey return in case of regular index.
        local lua_code = [[
            function(tuple)
                return {{"hello", "world"}, {1, 2}}
            end
        ]]
        box.schema.func.create('invalidreturn4', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        local idx = s:create_index('idx', {
            func = box.func.invalidreturn4.id,
            parts = {{1, 'unsigned'}, {2, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNC_INDEX_FORMAT',
            index = 'idx',
            space = 'test',
            details = 'key does not follow functional index definition',
        }, s.insert, s, {1})
        idx:drop()

        -- Invalid return format for multikey index 2.
        local lua_code = 'function(tuple) return "hello" end'
        box.schema.func.create('invalidreturn5', {
            body = lua_code, is_deterministic = true, is_sandboxed = true,
            opts = {is_multikey = true}
        })
        local idx = s:create_index('idx', {
            func = box.func.invalidreturn5.id,
            parts = {{1, 'unsigned'}, {2, 'unsigned'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNC_INDEX_FORMAT',
            index = 'idx',
            space = 'test',
            details = "a multikey function mustn't return a scalar",
        }, s.insert, s, {1})
        idx:drop()
    end)
end

-- Runtime extractor error: the error chain is checked.
g.test_func_index_extractor_runtime_error = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        local lua_code = [[
            function(tuple)
                local json = require('json')
                return json.encode(tuple)
            end
        ]]
        box.schema.func.create('runtimeerror', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        local idx = s:create_index('idx', {
            func = box.func.runtimeerror.id, parts = {{1, 'string'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNC_INDEX_FUNC',
            index = 'idx',
            space = 'test',
            message = "Failed to build a key for functional index 'idx' " ..
                      "of space 'test': can't evaluate function",
        }, s.insert, s, {1})
        local e = box.error.last()
        t.assert_equals(e.type, 'ClientError')
        e = e.prev
        t.assert_equals(e.type, 'LuajitError')
        t.assert_str_contains(e.message,
                              "attempt to call global 'require' " ..
                              '(a nil value)')
        t.assert_equals(e.prev, nil)
        idx:drop()
    end)
end

-- Func index test cases.
g.test_func_index = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local lua_code = 'function(tuple) return {tuple[1] + tuple[2]} end'
        box.schema.func.create('extr', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        s:create_index('pk')
        s:insert({1, 2})
        local idx = s:create_index('idx', {
            unique = true, func = 'extr', parts = {{1, 'integer'}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            name = 'TUPLE_FOUND',
            index = 'idx',
            space = 'test',
        }, s.insert, s, {2, 1})
        t.assert_equals(idx:get(3), {1, 2})
        t.assert_equals(idx:delete(3), {1, 2})
        t.assert_equals(s:select(), {})
        s:insert({2, 1})
        t.assert_equals(idx:get(3), {2, 1})
        s:replace({1, 1})
        s:replace({2, 2})
        s:replace({1, 2})
        t.assert_equals(idx:get(2), nil)
        t.assert_equals(idx:get(3), {1, 2})
        t.assert_equals(idx:get(4), {2, 2})
        t.assert_equals(idx:delete(3), {1, 2})
        t.assert_equals(idx:delete(4), {2, 2})
    end)
end

-- Multikey functional index.
g.test_func_index_multikey = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local lua_code = [[
            function(tuple)
                return {{tuple[1] + tuple[2]},
                        {tuple[1] + tuple[2]},
                        {tuple[1]}}
            end
        ]]
        box.schema.func.create('extr', {
            body = lua_code, is_deterministic = true, is_sandboxed = true,
            opts = {is_multikey = true}
        })
        s:create_index('pk')
        local idx = s:create_index('idx', {
            unique = true, func = box.func.extr.id, parts = {{1, 'integer'}}
        })
        s:insert({1, 2})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'TUPLE_FOUND',
            index = 'idx',
            space = 'test',
        }, s.insert, s, {3, 5})
        s:insert({5, 3})
        t.assert_equals(idx:select(), {{1, 2}, {1, 2}, {5, 3}, {5, 3}})
        t.assert_equals(idx:get(8), {5, 3})
        t.assert_equals(idx:get(3), {1, 2})
        t.assert_equals(idx:get(1), {1, 2})
        t.assert_equals(idx:get(5), {5, 3})
    end)
end

-- Multikey multipart functional index.
g.test_func_index_multikey_multipart = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local lua_code = [[
            function(tuple)
                return {{600 + tuple[1], 600 + tuple[2]},
                        {500 + tuple[1], 500 + tuple[2]}}
            end
        ]]
        box.schema.func.create('extr', {
            body = lua_code, is_deterministic = true, is_sandboxed = true,
            opts = {is_multikey = true}
        })
        s:create_index('pk')
        local idx = s:create_index('idx', {
            unique = true, func = box.func.extr.id,
            parts = {{1, 'integer'}, {2, 'integer'}}
        })
        s:insert({1, 2})
        s:insert({2, 1})
        s:insert({3, 3})
        t.assert_equals(idx:select({600}, {iterator = 'GE'}),
                        {{1, 2}, {2, 1}, {3, 3}})
        t.assert_equals(idx:get({603, 603}), {3, 3})
        t.assert_equals(idx:select({503}, {iterator = 'LE'}),
                        {{3, 3}, {2, 1}, {1, 2}})
    end)
end

-- Multikey non-unique functional index.
g.test_func_index_multikey_non_unique = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local lua_code = [[
            function(tuple)
                return {{500 + tuple[1]},
                        {500 + tuple[2]},
                        {500 + tuple[2]},
                        {500 + tuple[2]}}
            end
        ]]
        box.schema.func.create('extr', {
            body = lua_code, is_deterministic = true, is_sandboxed = true,
            opts = {is_multikey = true}
        })
        s:create_index('pk')
        s:create_index('idx', {
            unique = false, func = box.func.extr.id, parts = {{1, 'integer'}}
        })
        s:insert({1, 2})
        s:insert({2, 1})
        local idx = s.index.idx
        t.assert_equals(idx:select({501}), {{1, 2}, {2, 1}})
        t.assert_equals(idx:select({502}), {{1, 2}, {2, 1}})
        s:replace({1, 3})
        t.assert_equals(idx:select({501}), {{1, 3}, {2, 1}})
        t.assert_equals(idx:select({502}), {{2, 1}})
        t.assert_equals(idx:select({503}), {{1, 3}})
        box.snapshot()
    end)
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.test
        local idx = s.index.idx
        t.assert_equals(idx:select({501}), {{1, 3}, {2, 1}})
        t.assert_equals(idx:select({502}), {{2, 1}})
        t.assert_equals(idx:select({503}), {{1, 3}})
        s:replace({1, 2})
        t.assert_equals(idx:select({501}), {{1, 2}, {2, 1}})
        t.assert_equals(idx:select({502}), {{1, 2}, {2, 1}})
        t.assert_equals(idx:select({503}), {})
    end)
end

-- Multikey UTF-8 address extractor.
g.test_func_index_multikey_collation = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('name', {parts = {1, 'string'}})
        s:insert({'James', 'SIS Building Lambeth London UK'})
        s:insert({'Sherlock', '221B Baker St Marylebone London NW1 6XE UK'})
        -- Create functional index on space with data.
        local lua_code = [[
            function(tuple)
                local address = string.split(tuple[2])
                local ret = {}
                for _, v in pairs(address) do
                    table.insert(ret, {utf8.upper(v)})
                end
                return ret
            end
        ]]
        box.schema.func.create('addr_extractor', {
            body = lua_code, is_deterministic = true, is_sandboxed = true,
            opts = {is_multikey = true}
        })
        local idx = s:create_index('addr', {
            unique = false, func = box.func.addr_extractor.id,
            parts = {{1, 'string', collation = 'unicode_ci'}}
        })
        t.assert_equals(idx:select('uk'), {
            {'James', 'SIS Building Lambeth London UK'},
            {'Sherlock', '221B Baker St Marylebone London NW1 6XE UK'},
        })
        t.assert_equals(idx:select('Sis'), {
            {'James', 'SIS Building Lambeth London UK'},
        })
    end)
end

-- Partial index with functional index extractor.
g.test_func_index_partial = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        local lua_code = [[
            function(tuple)
                if tuple[1] % 2 == 1 then
                    return {{tuple[1]}}
                else
                    return {}
                end
            end
        ]]
        box.schema.func.create('extr', {
            body = lua_code, is_deterministic = true, is_sandboxed = true,
            opts = {is_multikey = true}
        })
        local idx = s:create_index('idx', {
            unique = true, func = box.func.extr.id, parts = {{1, 'integer'}}
        })
        s:insert({1})
        s:insert({2})
        s:insert({3})
        s:insert({4})
        t.assert_equals(idx:select(), {{1}, {3}})
    end)
end

-- Return nil from functional index extractor.
g.test_func_index_extractor_nil_return = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        local lua_code = 'function(tuple) return {nil} end'
        box.schema.func.create('extr', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        s:create_index('idx', {
            unique = false, func = box.func.extr.id,
            parts = {{1, 'integer', is_nullable = true}}
        })
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNC_INDEX_FORMAT',
            message = "Key format doesn't match one defined in functional " ..
                      "index 'idx' of space 'test': Invalid key part " ..
                      "count in an exact match (expected 1, got 0)",
        }, s.insert, s, {1})
    end)
end

-- Multiple functional indexes.
g.test_multiple_func_indexes = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local lua_code = 'function(tuple) return {tuple[1] + tuple[2]} end'
        box.schema.func.create('s', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        local lua_code = 'function(tuple) return {tuple[1] - tuple[2]} end'
        box.schema.func.create('sub', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        s:create_index('pk')
        local idx1 = s:create_index('s_idx', {
            unique = true, func = box.func.s.id, parts = {{1, 'integer'}}
        })
        local idx2 = s:create_index('sub_idx', {
            unique = true, func = box.func.sub.id, parts = {{1, 'integer'}}
        })
        s:insert({4, 1})
        t.assert_equals(idx1:get(5), {4, 1})
        t.assert_equals(idx2:get(3), {4, 1})
        idx1:drop()
        t.assert_equals(idx2:get(3), {4, 1})
    end)
end

--
-- gh-4401: make functional index creation transactional.
--
g.test_func_index_creation_is_transactional = function(cg)
    cg.server:exec(function()
        local lua_code = 'function(tuple) return {tuple[1] + tuple[2]} end'
        t.assert_error_covers({
            type = 'ClientError',
            name = 'WRONG_INDEX_OPTIONS',
            details = "Function ids defined in _index and _func_index " ..
                      "don't match",
        }, box.atomic, function()
            box.schema.func.create('extr', {
                body = lua_code, is_deterministic = true, is_sandboxed = true
            })
            box.schema.func.create('extr1', {
                body = lua_code, is_deterministic = true, is_sandboxed = true
            })
            local s = box.schema.space.create('test')
            s:create_index('pk')
            box.space._index:insert({s.id, 2, 'idx', 'tree',
                {unique = true, func = box.func.extr.id}, {{0, 'integer'}}})
            box.space._func_index:insert({s.id, 2, box.func.extr1.id})
        end)
        t.assert_equals(box.func.extr1, nil)
        t.assert_equals(box.func.extr, nil)
        t.assert_equals(box.is_in_txn(), false)
        t.assert_equals(box.space._space.index.name:count('test'), 0)
    end)
end

-- Test successful functional index creation in a transaction.
g.test_func_index_creation_in_txn = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local lua_code = 'function(tuple) return {tuple[1] + tuple[2]} end'
        box.schema.func.create('extr', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        s:create_index('pk')
        box.atomic(function()
            s:create_index('idx', {
                unique = true, func = 'extr', parts = {{1, 'integer'}}
            })
        end)
        s:insert({1, 2})
        t.assert_equals(s.index.idx:get({3}), {1, 2})
    end)
end

--
-- Function is added at alter.
--
g.test_func_index_func_added_at_alter = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local lua_code = [[
            function(tuple)
                return {tuple[2] >= 0 and tuple[2] or -tuple[2]}
            end
        ]]
        box.schema.func.create('second_field_module', {
            body = lua_code, is_deterministic = true, is_sandboxed = true
        })
        s:create_index('pk')
        local sk = s:create_index('sk', {parts = {{2, 'unsigned'}}})
        sk:alter({func = 'second_field_module', parts = {{1, 'unsigned'}}})
        s:insert({1, -3})
        t.assert_equals(sk:get({3}), {1, -3})
    end)
end
