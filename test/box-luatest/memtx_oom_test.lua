local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

local after_each = function(cg)
    cg.server:exec(function()
        local errinj = box.error.injection
        errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
        errinj.set('ERRINJ_INDEX_OOM', false)
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        rawset(_G, 'assert_equals_sorted', function(actual, expected)
            table.sort(actual, function(a, b) return a[1] < b[1] end)
            t.assert_equals(actual, expected)
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(after_each)

------------------
-- Txn code tests.
------------------

g.test_stmt_rollback_flag_cleared = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        s:create_index('pk')
        s:insert({1, 10})
        box.begin()
        s:insert({2, 20})
        errinj.set('ERRINJ_INDEX_OOM', true)
        t.assert_error_covers({
            type = 'OutOfMemory',
        }, s.insert, s, {3, 30})
        t.assert_error_covers({
            type = 'OutOfMemory',
        }, s.insert, s, {4, 40})
        box.commit()
        t.assert_equals(s:select(), {{1, 10}, {2, 20}})
    end)
end

--------------------
-- Tree index tests.
--------------------

g.test_rollback_tree_insert_oom = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        for c = 1, 20 do
            s:create_index('pk')
            s:create_index('sk1')
            s:create_index('sk2')
            s:insert({1, 10})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.insert, s, {2, 20})
            if ok then
                local expected = {{1, 10}, {2, 20}}
                t.assert_equals(s:select(), expected)
                t.assert_equals(s.index.sk1:select(), expected)
                t.assert_equals(s.index.sk2:select(), expected)
            else
                t.assert_equals(err:unpack().type, 'OutOfMemory')
                local expected = {{1, 10}}
                t.assert_equals(s:select(), expected)
                t.assert_equals(s.index.sk1:select(), expected)
                t.assert_equals(s.index.sk2:select(), expected)
            end
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk2:drop()
            s.index.sk1:drop()
            s.index.pk:drop()
        end
    end)
end

g.test_rollback_tree_delete_oom = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        for c = 1, 20 do
            s:create_index('pk')
            s:create_index('sk1')
            s:create_index('sk2')
            s:insert({1, 10})
            s:insert({2, 20})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.delete, s, {2})
            if ok then
                local expected = {{1, 10}}
                t.assert_equals(s:select(), expected)
                t.assert_equals(s.index.sk1:select(), expected)
                t.assert_equals(s.index.sk2:select(), expected)
            else
                t.assert_equals(err:unpack().type, 'OutOfMemory')
                local expected = {{1, 10}, {2, 20}}
                t.assert_equals(s:select(), expected)
                t.assert_equals(s.index.sk1:select(), expected)
                t.assert_equals(s.index.sk2:select(), expected)
            end
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk1:drop()
            s.index.sk2:drop()
            s.index.pk:drop()
        end
    end)
end

g.test_rollback_tree_replace_oom = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        for c = 1, 20 do
            s:create_index('pk')
            s:create_index('sk1', {parts = {2}})
            s:create_index('sk2', {parts = {2}})
            s:insert({1, 10})
            s:insert({2, 20})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.replace, s, {2, 200})
            if ok then
                local expected = {{1, 10}, {2, 200}}
                t.assert_equals(s:select(), expected)
                t.assert_equals(s.index.sk1:select(), expected)
                t.assert_equals(s.index.sk2:select(), expected)
            else
                t.assert_equals(err:unpack().type, 'OutOfMemory')
                local expected = {{1, 10}, {2, 20}}
                t.assert_equals(s:select(), expected)
                t.assert_equals(s.index.sk1:select(), expected)
                t.assert_equals(s.index.sk2:select(), expected)
            end
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk1:drop()
            s.index.sk2:drop()
            s.index.pk:drop()
        end
    end)
end

g.test_rollback_tree_insert_dup = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        for c = 1, 10 do
            s:create_index('pk')
            s:create_index('sk', {parts = {2}})
            s:insert({1, 10})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.insert, s, {2, 10})
            t.assert_not(ok)
            local u = err:unpack()
            t.assert(u.type == 'OutOfMemory' or
                     (u.type == 'ClientError' and
                      u.code == box.error.TUPLE_FOUND), u)
            t.assert_equals(s.index.pk:select(), {{1, 10}})
            t.assert_equals(s.index.sk:select(), {{1, 10}})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk:drop()
            s.index.pk:drop()
        end
    end)
end

g.test_rollback_tree_update_pk = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        for c = 1, 20 do
            s:create_index('pk')
            s:insert({1, 10})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.update, s, {1}, {{'=', 1, 2}})
            t.assert_not(ok)
            local u = err:unpack()
            t.assert(u.type == 'OutOfMemory' or
                     (u.type == 'ClientError' and
                      u.code == box.error.CANT_UPDATE_PRIMARY_KEY), u)
            t.assert_equals(s:select(), {{1, 10}})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.pk:drop()
        end
    end)
end

--------------------
-- Hash index tests.
--------------------

g.test_rollback_hash_insert_oom = function(cg)
    cg.server:exec(function()
        local errinj = box.error.injection
        local assert_equals_sorted = _G.assert_equals_sorted
        local s = box.schema.create_space('test')
        for c = 1, 20 do
            s:create_index('pk')
            s:create_index('sk1', {type = 'HASH', parts = {2}})
            s:create_index('sk2', {type = 'HASH', parts = {2}})
            s:insert({1, 10})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.insert, s, {2, 20})
            if ok then
                local expected = {{1, 10}, {2, 20}}
                assert_equals_sorted(s.index.pk:select(), expected)
                assert_equals_sorted(s.index.sk1:select(), expected)
                assert_equals_sorted(s.index.sk2:select(), expected)
            else
                t.assert_equals(err:unpack().type, 'OutOfMemory')
                local expected = {{1, 10}}
                t.assert_equals(s.index.pk:select(), expected)
                t.assert_equals(s.index.sk1:select(), expected)
                t.assert_equals(s.index.sk2:select(), expected)
            end
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk1:drop()
            s.index.sk2:drop()
            s.index.pk:drop()
        end
    end)
end

g.test_rollback_hash_delete_oom = function(cg)
    cg.server:exec(function()
        local errinj = box.error.injection
        local assert_equals_sorted = _G.assert_equals_sorted
        local s = box.schema.create_space('test')
        for c = 1, 20 do
            s:create_index('pk')
            s:create_index('sk1', {type = 'HASH', parts = {2}})
            s:create_index('sk2', {type = 'HASH', parts = {2}})
            s:insert({1, 10})
            s:insert({2, 20})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.delete, s, {2})
            if ok then
                local expected = {{1, 10}}
                t.assert_equals(s.index.pk:select(), expected)
                t.assert_equals(s.index.sk1:select(), expected)
                t.assert_equals(s.index.sk2:select(), expected)
            else
                t.assert_equals(err:unpack().type, 'OutOfMemory')
                local expected = {{1, 10}, {2, 20}}
                assert_equals_sorted(s.index.pk:select(), expected)
                assert_equals_sorted(s.index.sk1:select(), expected)
                assert_equals_sorted(s.index.sk2:select(), expected)
            end
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk1:drop()
            s.index.sk2:drop()
            s.index.pk:drop()
        end
    end)
end

g.test_rollback_hash_replace_oom = function(cg)
    cg.server:exec(function()
        local errinj = box.error.injection
        local assert_equals_sorted = _G.assert_equals_sorted
        local s = box.schema.create_space('test')
        for c = 1, 20 do
            s:create_index('pk')
            s:create_index('sk1', {type = 'HASH', parts = {2}})
            s:create_index('sk2', {type = 'HASH', parts = {2}})
            s:insert({1, 10})
            s:insert({2, 20})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.replace, s, {2, 21})
            if ok then
                local expected = {{1, 10}, {2, 21}}
                assert_equals_sorted(s.index.pk:select(), expected)
                assert_equals_sorted(s.index.sk1:select(), expected)
                assert_equals_sorted(s.index.sk2:select(), expected)
            else
                t.assert_equals(err:unpack().type, 'OutOfMemory')
                local expected = {{1, 10}, {2, 20}}
                assert_equals_sorted(s.index.pk:select(), expected)
                assert_equals_sorted(s.index.sk1:select(), expected)
                assert_equals_sorted(s.index.sk2:select(), expected)
            end
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk1:drop()
            s.index.sk2:drop()
            s.index.pk:drop()
        end
    end)
end

g.test_rollback_hash_insert_dup = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        for c = 1, 20 do
            s:create_index('pk')
            s:create_index('sk1', {type = 'HASH', parts = {2}})
            s:create_index('sk2', {type = 'HASH', parts = {2}})
            s:insert({1, 10})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.insert, s, {2, 10})
            t.assert_not(ok)
            local u = err:unpack()
            t.assert(u.type == 'OutOfMemory' or
                     (u.type == 'ClientError' and
                      u.code == box.error.TUPLE_FOUND), u)
            t.assert_equals(s.index.pk:select(), {{1, 10}})
            t.assert_equals(s.index.sk1:select(), {{1, 10}})
            t.assert_equals(s.index.sk2:select(), {{1, 10}})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk1:drop()
            s.index.sk2:drop()
            s.index.pk:drop()
        end
    end)
end

g.test_rollback_hash_update_pk = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        for c = 1, 20 do
            s:create_index('pk', {type = 'HASH'})
            s:insert({1, 10})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.update, s, {1}, {{'=', 1, 2}})
            t.assert_not(ok)
            local u = err:unpack()
            t.assert(u.type == 'OutOfMemory' or
                     (u.type == 'ClientError' and
                      u.code == box.error.CANT_UPDATE_PRIMARY_KEY), u)
            t.assert_equals(s:select(), {{1, 10}})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.pk:drop()
        end
    end)
end

-- gh-1117
g.test_rollback_trigger_failure = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:format({
            {name = 'a', type = 'unsigned'},
            {name = 'b', 'unsigned', is_nullable = true}
        })
        s:create_index('pk', {type = 'HASH'})
        s:insert({1, 10})
        s:insert({2, box.NULL})
        local errinj = box.error.injection
        for c = 1, 20 do
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.format, s, {
                {name = 'a', type = 'unsigned'},
                {name = 'b', type = 'unsigned'}
            })
            t.assert_not(ok)
            local u = err:unpack()
            t.assert(u.type == 'OutOfMemory' or
                     (u.type == 'ClientError' and
                      u.code == box.error.FIELD_TYPE and
                      u.expected == 'unsigned' and
                      u.actual == 'nil'), u)
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
        end
    end)
end

g.test_rollback_to_svp = function(cg)
    cg.server:exec(function()
        local errinj = box.error.injection
        local s = box.schema.create_space('test')
        for c = 1, 20 do
            s:create_index('pk')
            local f = function()
                box.begin()
                s:insert({1, 10})
                local svp = box.savepoint()
                s:insert({2, 20})
                box.rollback_to_savepoint(svp)
                box.commit()
            end
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(f)
            if ok then
                t.assert_equals(s:select(), {{1, 10}})
            else
                box.rollback()
                t.assert_equals(err:unpack().type, 'OutOfMemory')
                t.assert_equals(s:select(), {})
            end
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.pk:drop()
        end
    end)
end

-----------------------------------
-- Multikey/functional index tests.
-----------------------------------

local g_mk = t.group('multikey', {
    {index_options = {parts = {{field = 2, path = '[*]'}}, unique = true}},
    {index_options = {parts = {{1, 'unsigned'}}, func = 'test'}}
})

g_mk.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
    box.schema.func.create('test', {
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

g_mk.after_all(function(cg)
    cg.server:drop()
end)

g_mk.after_each(after_each)

g_mk.test_rollback_tree_insert_oom = function(cg)
    cg.server:exec(function(index_options)
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        for c = 1, 20 do
            s:create_index('pk')
            s:create_index('sk1', index_options)
            s:create_index('sk2', index_options)
            s:insert({1, {10}})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.insert, s, {2, {20, 30}})
            if ok then
                local expected_pk = {{1, {10}}, {2, {20, 30}}}
                local expected_sk = {{1, {10}}, {2, {20, 30}}, {2, {20, 30}}}
                t.assert_equals(s:select(), expected_pk)
                t.assert_equals(s.index.sk1:select(), expected_sk)
                t.assert_equals(s.index.sk2:select(), expected_sk)
            else
                local expected = {{1, {10}}}
                t.assert_equals(err:unpack().type, 'OutOfMemory')
                t.assert_equals(s:select(), expected)
                t.assert_equals(s.index.sk1:select(), expected)
                t.assert_equals(s.index.sk2:select(), expected)
            end
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk1:drop()
            s.index.sk2:drop()
            s.index.pk:drop()
        end
    end, {cg.params.index_options})
end

g_mk.test_rollback_tree_delete_oom = function(cg)
    cg.server:exec(function(index_options)
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        for c = 1, 100 do
            s:create_index('pk')
            s:create_index('sk1', index_options)
            s:create_index('sk2', index_options)
            s:insert({1, {10, 11}, 100})
            s:insert({2, {20, 21}, 200})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.delete, s, {2})
            if ok then
                local expected_pk = {{1, {10, 11}, 100}}
                local expected_sk = {{1, {10, 11}, 100}, {1, {10, 11}, 100}}
                t.assert_equals(s:select(), expected_pk)
                t.assert_equals(s.index.sk1:select(), expected_sk)
                t.assert_equals(s.index.sk2:select(), expected_sk)
            else
                local expected_pk = {{1, {10, 11}, 100}, {2, {20, 21}, 200}}
                local expected_sk = {
                    {1, {10, 11}, 100}, {1, {10, 11}, 100},
                    {2, {20, 21}, 200}, {2, {20, 21}, 200},
                }
                t.assert_equals(err:unpack().type, 'OutOfMemory')
                t.assert_equals(s:select(), expected_pk)
                t.assert_equals(s.index.sk1:select(), expected_sk)
                t.assert_equals(s.index.sk2:select(), expected_sk)
            end
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk1:drop()
            s.index.sk2:drop()
            s.index.pk:drop()
        end
    end, {cg.params.index_options})
end

g_mk.test_rollback_tree_replace_oom = function(cg)
    cg.server:exec(function(index_options)
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        for c = 1, 100 do
            s:create_index('pk')
            s:create_index('sk1', index_options)
            s:create_index('sk2', index_options)
            s:insert({1, {10, 20, 30}, 100})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.replace, s, {1, {15, 20, 25}, 200})
            if ok then
                local expected_pk = {{1, {15, 20, 25}, 200}}
                local expected_sk = {
                    {1, {15, 20, 25}, 200}, {1, {15, 20, 25}, 200},
                    {1, {15, 20, 25}, 200}
                }
                t.assert_equals(s:select(), expected_pk)
                t.assert_equals(s.index.sk1:select(), expected_sk)
                t.assert_equals(s.index.sk2:select(), expected_sk)
            else
                local expected_pk = {{1, {10, 20, 30}, 100}}
                local expected_sk = {
                    {1, {10, 20, 30}, 100}, {1, {10, 20, 30}, 100},
                    {1, {10, 20, 30}, 100}
                }
                t.assert_equals(err:unpack().type, 'OutOfMemory')
                t.assert_equals(s:select(), expected_pk)
                t.assert_equals(s.index.sk1:select(), expected_sk)
                t.assert_equals(s.index.sk2:select(), expected_sk)
            end
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk1:drop()
            s.index.sk2:drop()
            s.index.pk:drop()
        end
    end, {cg.params.index_options})
end

g_mk.test_rollback_tree_insert_dup = function(cg)
    cg.server:exec(function(index_options)
        local s = box.schema.create_space('test')
        local errinj = box.error.injection
        for c = 1, 100 do
            s:create_index('pk')
            s:create_index('sk', index_options)
            s:insert({1, {10}})
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', c)
            local ok, err = pcall(s.insert, s, {2, {10}})
            t.assert_not(ok)
            local u = err:unpack()
            t.assert(u.type == 'OutOfMemory' or
                     (u.type == 'ClientError' and
                      u.code == box.error.TUPLE_FOUND), u)
            local expected = {{1, {10}}}
            t.assert_equals(s:select(), expected)
            t.assert_equals(s.index.sk:select(), expected)
            errinj.set('ERRINJ_INDEX_OOM_COUNTDOWN', -1)
            errinj.set('ERRINJ_INDEX_OOM', false)
            s.index.sk:drop()
            s.index.pk:drop()
        end
    end, {cg.params.index_options})
end
