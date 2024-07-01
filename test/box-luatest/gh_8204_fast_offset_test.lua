local server = require('luatest.server')
local t = require('luatest')

local g_generic = t.group('gh-8204-generic')
local g_mvcc = t.group('gh-8204-mvcc')
local g_perf = t.group('gh-8204-perf')

g_generic.before_all(function()
    g_generic.server = server:new({ alias = 'master' })
    g_generic.server:start()
end)

g_mvcc.before_all(function()
    g_mvcc.server = server:new({
        alias = 'master',
        box_cfg = { memtx_use_mvcc_engine = true }
    })
    g_mvcc.server:start()
end)

g_perf.before_all(function()
    g_perf.server = server:new({
        alias = 'master',
        box_cfg = { wal_mode = 'none' } -- Fill spaces faster.
    })
    g_perf.server:start()
end)

for _, g in pairs({g_generic, g_mvcc, g_perf}) do
    g.after_all(function()
        g.server:drop()
    end)
end

for _, g in pairs({g_generic, g_perf}) do
    g.after_each(function()
        g.server:exec(function()
            if box.space.test then
                box.space.test:drop()
            end
        end)
    end)
end

g_mvcc.after_each(function()
    g_mvcc.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end

        if box.space.make_conflicting_writer then
            box.space.make_conflicting_writer:drop()
        end
    end)
end)

g_generic.test_option = function()
    g_generic.server:exec(function()
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
    end)
end

g_generic.test_count = function()
    g_generic.server:exec(function()
        -- A space with fast_offset secondary key (so we can check nulls).
        local s = box.schema.space.create('test')
        s:create_index('pk')
        local sk = s:create_index('sk', {fast_offset = true,
                                   parts = {{2, 'uint64', is_nullable = true},
                                            {3, 'uint64', is_nullable = true}}})

        -- A helper function for verbose assertion using pretty printer.
        local function check(it, key, expected_count)
            local pp = require('luatest.pp')

            -- The location of the callee.
            local file = debug.getinfo(2,'S').source
            local line = debug.getinfo(2, 'l').currentline

            -- The stringified key.
            local key_str = pp.tostring(key)

            t.assert_equals(sk:count(key, {iterator = it}), expected_count,
                            string.format('\nkey: %s,\niterator: %s,' ..
                                          '\nfile: %s,\nline: %d,',
                                          key_str, it, file, line))
        end

        -- Test the empty space.
        for _, it in pairs({'lt', 'le', 'eq', 'req', 'ge', 'gt', 'all'}) do
            check(it, {}, 0)
            check(it, {box.NULL}, 0)
            check(it, {0}, 0)
            check(it, {1}, 0)
            check(it, {1, box.NULL}, 0)
            check(it, {1, 0}, 0)
            check(it, {1, 1}, 0)
        end

        -- Fill the space.
        s:insert({1, 1, 1})
        s:insert({2, 1, 2})
        s:insert({3, 2, 1})
        s:insert({4, 2, 2})
        s:insert({5, 3, 1})
        s:insert({6, 3, 2})
        t.assert_equals(s:count(), 6)

        -- Empty key always returns the space size.
        for _, it in pairs({'lt', 'le', 'eq', 'req', 'ge', 'gt', 'all'}) do
            check(it, {}, s:count())
        end

        -- GE, ALL (it's identical to GE according to the documentation).
        for _, it in pairs({'ge', 'all'}) do
            check(it, {box.NULL}, 6)
            check(it, {1}, 6)
            check(it, {1, 1}, 6)
            check(it, {1, 2}, 5)
            check(it, {1, 3}, 4)
            check(it, {2}, 4)
            check(it, {2, box.NULL}, 4)
            check(it, {2, 1}, 4)
            check(it, {2, 2}, 3)
            check(it, {3, 1}, 2)
            check(it, {3, 2}, 1)
            check(it, {3, 3}, 0)
            check(it, {4}, 0)
        end

        -- GT.
        check('gt', {box.NULL}, 6)
        check('gt', {1}, 4)
        check('gt', {2}, 2)
        check('gt', {2, 1}, 3)
        check('gt', {2, 2}, 2)
        check('gt', {2, box.NULL}, 4)
        check('gt', {3, 1}, 1)
        check('gt', {3, 2}, 0)
        check('gt', {3, 3}, 0)
        check('gt', {3}, 0)

        -- LE.
        check('le', {3}, 6)
        check('le', {3, 2}, 6)
        check('le', {3, 1}, 5)
        check('le', {3, box.NULL}, 4)
        check('le', {2}, 4)
        check('le', {2, 2}, 4)
        check('le', {2, 1}, 3)
        check('le', {2, box.NULL}, 2)
        check('le', {1}, 2)
        check('le', {0}, 0)
        check('le', {box.NULL}, 0)

        -- LT.
        check('lt', {4}, 6)
        check('lt', {3, 3}, 6)
        check('lt', {3, 2}, 5)
        check('lt', {3, 1}, 4)
        check('lt', {3}, 4)
        check('lt', {2, 2}, 3)
        check('lt', {2, 1}, 2)
        check('lt', {2}, 2)
        check('lt', {2, box.NULL}, 2)
        check('lt', {1}, 0)
        check('lt', {0}, 0)
        check('lt', {box.NULL}, 0)

        -- EQ/REQ.
        for _, it in pairs({'eq', 'req'}) do
            check(it, {box.NULL}, 0)
            for _, key in pairs({1, 2, 3}) do
                check(it, {key}, 2)
                check(it, {key, 1}, 1)
                check(it, {key, 2}, 1)
                check(it, {key, box.NULL}, 0)
            end
        end
    end)
end

g_mvcc.test_count_manual = function()
    g_mvcc.server:exec(function()
        -- The test space.
        local s = box.schema.space.create('test')
        s:create_index('pk', {fast_offset = true,
                              parts = {{1, 'unsigned'}, {2, 'unsigned'}}})

        -- Create a space to make tested transactions writing - only writing
        -- transactions can cause conflicts with aborts.
        box.schema.space.create('make_conflicting_writer')
        box.space.make_conflicting_writer:create_index('pk', {sequence = true})

        local all_iterators = {'lt', 'le', 'eq', 'req', 'ge', 'gt', 'all'}
        local existing_keys = {{1, 1}, {1, 2}, {2, 1}, {2, 2}, {3, 1}, {3, 2}}
        local unexisting_keys = {{0, 0}, {0, 1}, {0, 2}, {0, 3},
                                 {1, 0}, {1, 3}, {2, 0}, {2, 3}, {3, 0}, {3, 3},
                                 {4, 0}, {4, 1}, {4, 2}, {4, 3}}
        local all_test_keys = {{},
                               {0}, {0, 0}, {0, 1}, {0, 2}, {0, 3},
                               {1}, {1, 0}, {1, 1}, {1, 2}, {1, 3},
                               {2}, {2, 0}, {2, 1}, {2, 2}, {2, 3},
                               {3}, {3, 0}, {3, 1}, {3, 2}, {3, 3},
                               {4}, {4, 0}, {4, 1}, {4, 2}, {4, 3}}

        -- Prepare proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
        local tx = txn_proxy.new()
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()

        -- Proxy helpers.
        local conflict = {{error = "Transaction has been aborted by conflict"}}
        local success = ''

        -- Stringify a value to use in lua code string.
        -- E. g. array of 2 elements is transformed into "{1, 2}" string.
        local function to_lua_code(value)
            return require('luatest.pp').tostring(value)
        end

        -- Check if count with given key and iterator gives the expected result
        -- for the given transaction.
        local function check(tx, it, key, expected_count, file, line)
            -- The location of the callee.
            local file = file or debug.getinfo(2,'S').source
            local line = line or debug.getinfo(2, 'l').currentline

            -- The stringified key.
            local key = to_lua_code(key)

            local code = string.format('box.space.test.index.pk:count(%s, ' ..
                                       '{iterator = "%s"})', key, it)

            local comment = string.format('\nkey: %s,\niterator: %s,' ..
                                          '\nfile: %s,\nline: %d,',
                                          key, it, file, line)

            local ok, res = pcall(tx, code)
            t.assert(ok, comment)
            t.assert_equals(res, {expected_count}, comment)
        end

        -- Make the tx1 open a transaction and count by given it and key,
        -- then make the tx2 insert/delete (op) the given tuple,
        -- then make the tx1 writing to make it abort on conflict,
        -- then make the tx1 commit its transaction and expect tx1_result.
        --
        -- The tuple inserted by tx2 is cleaned up, the "writer" space is not.
        local function count_do(tx1, tx2, it, key, expected_count, op, tuple,
                                tx1_result)
            assert(op == 'insert' or op == 'delete')

            local old_len = s:len()
            local tuple_existed = s:count(tuple) ~= 0

            -- The location of the callee.
            local file = debug.getinfo(2,'S').source
            local line = debug.getinfo(2, 'l').currentline

            tx1:begin()
            check(tx1, it, key, expected_count, file, line);
            tx2(string.format('box.space.test:%s(%s)', op, to_lua_code(tuple)))
            tx1('box.space.make_conflicting_writer:insert({nil})')
            t.assert_equals(tx1:commit(), tx1_result, string.format(
                            '\nkey: %s\niterator: %s\noperation: %s' ..
                            '\ntuple: %s\nfile: %s,\nline: %s',
                            to_lua_code(key), it, op, to_lua_code(tuple),
                            file, line))

            -- Cleanup.
            local tuple_exists = s:count(tuple) ~= 0
            if op == 'insert' and not tuple_existed and tuple_exists then
                s:delete(tuple)
            elseif op == 'delete' and tuple_existed and not tuple_exists then
                s:insert(tuple)
            end
            t.assert_equals(s:len(), old_len)
        end

        -- Check for consistency of count with the given key and iterator in the
        -- given transaction: first performs a count, and then performs inserts
        -- and deletes of keys and checks if the count result remains the same.
        --
        -- If the transaction is nil, starts and commits a new one.
        local function check_consistency(tx_arg, it, key, expected_count)
            -- The location of the callee.
            local file = debug.getinfo(2,'S').source
            local line = debug.getinfo(2, 'l').currentline

            local existing_keys = s:select()
            local old_len = #existing_keys
            local tx = tx_arg or txn_proxy.new()

            -- Start a transaction manually if no passed.
            if tx_arg == nil then
                tx:begin()
            end

            check(tx, it, key, expected_count, file, line)
            for _, new_key in pairs(unexisting_keys) do
                s:insert(new_key)
                check(tx, it, key, expected_count, file, line)
            end
            for _, old_key in pairs(existing_keys) do
                s:delete(old_key)
                check(tx, it, key, expected_count, file, line)
            end
            for _, old_key in pairs(unexisting_keys) do
                s:delete(old_key)
                check(tx, it, key, expected_count, file, line)
            end
            for _, new_key in pairs(existing_keys) do
                s:insert(new_key)
                check(tx, it, key, expected_count, file, line)
            end

            -- Autocommit if no transaction passed.
            if tx_arg == nil then
                t.assert_equals(tx:commit(), success)
            end

            t.assert_equals(s:len(), old_len)
            t.assert_equals(s:select(), existing_keys)
        end

        -- No conflict with itself (count all in empty & insert).
        for _, it in pairs(all_iterators) do
            count_do(tx, tx, it, {}, 0, 'insert', {37, 37}, success)
        end

        -- No conflict with itself (count empty by key & insert matching).
        count_do(tx, tx, 'lt', {0, 1}, 0, 'insert', {0, 0}, success)
        count_do(tx, tx, 'le', {0, 0}, 0, 'insert', {0, 0}, success)
        count_do(tx, tx, 'req', {0, 0}, 0, 'insert', {0, 0}, success)
        count_do(tx, tx, 'eq', {0, 0}, 0, 'insert', {0, 0}, success)
        count_do(tx, tx, 'ge', {0, 0}, 0, 'insert', {0, 0}, success)
        count_do(tx, tx, 'gt', {0, 0}, 0, 'insert', {0, 1}, success)

        -- No conflict with itself (count empty by key & insert not matching).
        count_do(tx, tx, 'lt', {0, 0}, 0, 'insert', {0, 0}, success)
        count_do(tx, tx, 'le', {0, 0}, 0, 'insert', {0, 1}, success)
        count_do(tx, tx, 'req', {0, 0}, 0, 'insert', {1, 1}, success)
        count_do(tx, tx, 'eq', {0, 0}, 0, 'insert', {1, 1}, success)
        count_do(tx, tx, 'ge', {0, 1}, 0, 'insert', {0, 0}, success)
        count_do(tx, tx, 'gt', {0, 0}, 0, 'insert', {0, 0}, success)

        -- Conflict (count all in empty & insert).
        for _, it in pairs(all_iterators) do
            count_do(tx1, tx2, it, {}, 0, 'insert', {37, 37}, conflict)
        end

        -- Conflict (count empty by key & insert matching).
        count_do(tx1, tx2, 'lt', {0, 1}, 0, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'le', {0, 0}, 0, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'req', {0, 0}, 0, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'eq', {0, 0}, 0, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'ge', {0, 0}, 0, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'gt', {0, 0}, 0, 'insert', {0, 1}, conflict)

        -- No conflict (count empty by key & insert not matching).
        count_do(tx1, tx2, 'lt', {0, 0}, 0, 'insert', {0, 0}, success)
        count_do(tx1, tx2, 'le', {0, 0}, 0, 'insert', {0, 1}, success)
        count_do(tx1, tx2, 'req', {0, 0}, 0, 'insert', {1, 1}, success)
        count_do(tx1, tx2, 'eq', {0, 0}, 0, 'insert', {1, 1}, success)
        count_do(tx1, tx2, 'ge', {0, 1}, 0, 'insert', {0, 0}, success)
        count_do(tx1, tx2, 'gt', {0, 0}, 0, 'insert', {0, 0}, success)

        -- Consistency in the empty read view.
        for _, it in pairs(all_iterators) do
            for _, key in pairs(all_test_keys) do
                check_consistency(nil, it, key, 0)
            end
        end

        -- Consistency in the empty read view (multiple counts).
        tx:begin()
        for _, it in pairs(all_iterators) do
            for _, key in pairs(all_test_keys) do
                check_consistency(tx, it, key, 0)
            end
        end
        t.assert_equals(tx:commit(), success)

        -- Fill the space.
        for _, key in pairs(existing_keys) do
            s:insert(key)
        end

        -- No conflict with itself (count all & replace).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(existing_keys) do
                count_do(tx, tx, it, {}, 6, 'insert', key, success)
            end
        end

        -- No conflict with itself (count all & insert).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(unexisting_keys) do
                count_do(tx, tx, it, {}, 6, 'insert', key, success)
            end
        end

        -- No conflict with itself (count all & delete).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(existing_keys) do
                count_do(tx, tx, it, {}, 6, 'delete', key, success)
            end
        end

        -- No conflict with itself (count by key & replace matching).
        count_do(tx, tx, 'lt', {1, 2}, 1, 'insert', {1, 1}, success)
        count_do(tx, tx, 'le', {1, 2}, 2, 'insert', {1, 1}, success)
        count_do(tx, tx, 'req', {1}, 2, 'insert', {1, 1}, success)
        count_do(tx, tx, 'eq', {1}, 2, 'insert', {1, 1}, success)
        count_do(tx, tx, 'ge', {1, 1}, 6, 'insert', {1, 1}, success)
        count_do(tx, tx, 'gt', {1, 1}, 5, 'insert', {1, 2}, success)

        -- No conflict with itself (count by key & insert matching).
        count_do(tx, tx, 'lt', {1, 2}, 1, 'insert', {1, 0}, success)
        count_do(tx, tx, 'le', {1, 2}, 2, 'insert', {1, 0}, success)
        count_do(tx, tx, 'req', {1}, 2, 'insert', {1, 0}, success)
        count_do(tx, tx, 'eq', {1}, 2, 'insert', {1, 0}, success)
        count_do(tx, tx, 'ge', {1, 1}, 6, 'insert', {1, 3}, success)
        count_do(tx, tx, 'gt', {1, 1}, 5, 'insert', {1, 3}, success)

        -- No conflict with itself (count by key & delete matching).
        count_do(tx, tx, 'lt', {1, 2}, 1, 'delete', {1, 1}, success)
        count_do(tx, tx, 'le', {1, 2}, 2, 'delete', {1, 1}, success)
        count_do(tx, tx, 'req', {1}, 2, 'delete', {1, 1}, success)
        count_do(tx, tx, 'eq', {1}, 2, 'delete', {1, 1}, success)
        count_do(tx, tx, 'ge', {1, 1}, 6, 'delete', {1, 1}, success)
        count_do(tx, tx, 'gt', {1, 1}, 5, 'delete', {1, 2}, success)

        -- No conflict with itself (count by key & replace not matching).
        count_do(tx, tx, 'lt', {1, 2}, 1, 'insert', {2, 1}, success)
        count_do(tx, tx, 'le', {1, 2}, 2, 'insert', {2, 1}, success)
        count_do(tx, tx, 'req', {1}, 2, 'insert', {2, 1}, success)
        count_do(tx, tx, 'eq', {1}, 2, 'insert', {2, 1}, success)
        count_do(tx, tx, 'ge', {2, 1}, 4, 'insert', {1, 1}, success)
        count_do(tx, tx, 'gt', {2, 1}, 3, 'insert', {1, 2}, success)

        -- No conflict with itself (count by key & insert not matching).
        count_do(tx, tx, 'lt', {1, 2}, 1, 'insert', {2, 0}, success)
        count_do(tx, tx, 'le', {1, 2}, 2, 'insert', {2, 0}, success)
        count_do(tx, tx, 'req', {1}, 2, 'insert', {2, 0}, success)
        count_do(tx, tx, 'eq', {1}, 2, 'insert', {2, 0}, success)
        count_do(tx, tx, 'ge', {2, 1}, 4, 'insert', {1, 0}, success)
        count_do(tx, tx, 'gt', {2, 1}, 3, 'insert', {1, 0}, success)

        -- No conflict with itself (count by key & delete not matching).
        count_do(tx, tx, 'lt', {1, 2}, 1, 'delete', {2, 1}, success)
        count_do(tx, tx, 'le', {1, 2}, 2, 'delete', {2, 1}, success)
        count_do(tx, tx, 'req', {1}, 2, 'delete', {2, 1}, success)
        count_do(tx, tx, 'eq', {1}, 2, 'delete', {2, 1}, success)
        count_do(tx, tx, 'ge', {2, 1}, 4, 'delete', {1, 1}, success)
        count_do(tx, tx, 'gt', {2, 1}, 3, 'delete', {1, 2}, success)

        -- No conflict (count all & replace).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(existing_keys) do
                count_do(tx1, tx2, it, {}, 6, 'insert', key, success)
            end
        end

        -- Conflict (count all & insert).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(unexisting_keys) do
                count_do(tx1, tx2, it, {}, 6, 'insert', key, conflict)
            end
        end

        -- Conflict (count all & delete).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(existing_keys) do
                count_do(tx1, tx2, it, {}, 6, 'delete', key, conflict)
            end
        end

        -- No conflict (count by key & replace matching).
        count_do(tx1, tx2, 'lt', {1, 2}, 1, 'insert', {1, 1}, success)
        count_do(tx1, tx2, 'le', {1, 2}, 2, 'insert', {1, 1}, success)
        count_do(tx1, tx2, 'req', {1}, 2, 'insert', {1, 1}, success)
        count_do(tx1, tx2, 'eq', {1}, 2, 'insert', {1, 1}, success)
        count_do(tx1, tx2, 'ge', {1, 1}, 6, 'insert', {1, 1}, success)
        count_do(tx1, tx2, 'gt', {1, 1}, 5, 'insert', {1, 2}, success)

        -- Conflict (count by key & insert matching).
        count_do(tx1, tx2, 'lt', {1, 2}, 1, 'insert', {1, 0}, conflict)
        count_do(tx1, tx2, 'le', {1, 2}, 2, 'insert', {1, 0}, conflict)
        count_do(tx1, tx2, 'req', {1}, 2, 'insert', {1, 0}, conflict)
        count_do(tx1, tx2, 'eq', {1}, 2, 'insert', {1, 0}, conflict)
        count_do(tx1, tx2, 'ge', {1, 1}, 6, 'insert', {1, 3}, conflict)
        count_do(tx1, tx2, 'gt', {1, 1}, 5, 'insert', {1, 3}, conflict)

        -- Conflict (count by key & delete matching).
        count_do(tx1, tx2, 'lt', {1, 2}, 1, 'delete', {1, 1}, conflict)
        count_do(tx1, tx2, 'le', {1, 2}, 2, 'delete', {1, 1}, conflict)
        count_do(tx1, tx2, 'req', {1}, 2, 'delete', {1, 1}, conflict)
        count_do(tx1, tx2, 'eq', {1}, 2, 'delete', {1, 1}, conflict)
        count_do(tx1, tx2, 'ge', {1, 1}, 6, 'delete', {1, 1}, conflict)
        count_do(tx1, tx2, 'gt', {1, 1}, 5, 'delete', {1, 2}, conflict)

        -- No conflict (count by key & delete matching unexisting).
        count_do(tx1, tx2, 'lt', {1, 2}, 1, 'delete', {1, 0}, success)
        count_do(tx1, tx2, 'le', {1, 2}, 2, 'delete', {1, 0}, success)
        count_do(tx1, tx2, 'req', {1}, 2, 'delete', {1, 0}, success)
        count_do(tx1, tx2, 'eq', {1}, 2, 'delete', {1, 0}, success)
        count_do(tx1, tx2, 'ge', {1, 1}, 6, 'delete', {1, 3}, success)
        count_do(tx1, tx2, 'gt', {1, 1}, 5, 'delete', {1, 3}, success)


        -- No conflict (count by key & replace not matching).
        count_do(tx1, tx2, 'lt', {1, 2}, 1, 'insert', {2, 1}, success)
        count_do(tx1, tx2, 'le', {1, 2}, 2, 'insert', {2, 1}, success)
        count_do(tx1, tx2, 'req', {1}, 2, 'insert', {2, 1}, success)
        count_do(tx1, tx2, 'eq', {1}, 2, 'insert', {2, 1}, success)
        count_do(tx1, tx2, 'ge', {2, 1}, 4, 'insert', {1, 1}, success)
        count_do(tx1, tx2, 'gt', {2, 1}, 3, 'insert', {1, 2}, success)

        -- No conflict (count by key & insert not matching).
        count_do(tx1, tx2, 'lt', {1, 2}, 1, 'insert', {2, 0}, success)
        count_do(tx1, tx2, 'le', {1, 2}, 2, 'insert', {2, 0}, success)
        count_do(tx1, tx2, 'req', {1}, 2, 'insert', {2, 0}, success)
        count_do(tx1, tx2, 'eq', {1}, 2, 'insert', {2, 0}, success)
        count_do(tx1, tx2, 'ge', {2, 1}, 4, 'insert', {1, 0}, success)
        count_do(tx1, tx2, 'gt', {2, 1}, 3, 'insert', {1, 0}, success)

        -- No conflict (count by key & delete not matching).
        count_do(tx1, tx2, 'lt', {1, 2}, 1, 'delete', {2, 1}, success)
        count_do(tx1, tx2, 'le', {1, 2}, 2, 'delete', {2, 1}, success)
        count_do(tx1, tx2, 'req', {1}, 2, 'delete', {2, 1}, success)
        count_do(tx1, tx2, 'eq', {1}, 2, 'delete', {2, 1}, success)
        count_do(tx1, tx2, 'ge', {2, 1}, 4, 'delete', {1, 1}, success)
        count_do(tx1, tx2, 'gt', {2, 1}, 3, 'delete', {1, 2}, success)

        -- Consistency in the read view (count all).
        for _, it in pairs(all_iterators) do
            check_consistency(nil, it, {}, 6)
        end

        -- Consistency in the read view (count all multiple times).
        tx:begin()
        for _, it in pairs(all_iterators) do
            check_consistency(tx, it, {}, 6)
        end
        t.assert_equals(tx:commit(), success)

        -- Consistency in the read view (count by key).
        check_consistency(nil, 'lt', {1, 2}, 1)
        check_consistency(nil, 'le', {1, 2}, 2)
        check_consistency(nil, 'req', {1}, 2)
        check_consistency(nil, 'eq', {1}, 2)
        check_consistency(nil, 'ge', {2, 1}, 4)
        check_consistency(nil, 'gt', {2, 1}, 3)

        -- Consistency in the read view (count by key multiple times).
        tx:begin()
        check_consistency(tx, 'lt', {1, 2}, 1)
        check_consistency(tx, 'le', {1, 2}, 2)
        check_consistency(tx, 'req', {1}, 2)
        check_consistency(tx, 'eq', {1}, 2)
        check_consistency(tx, 'ge', {2, 1}, 4)
        check_consistency(tx, 'gt', {2, 1}, 3)
        t.assert_equals(tx:commit(), success)
    end)
end

g_mvcc.after_test('test_count_auto', function()
    g_mvcc.server:exec(function()
        if box.space.gold then
            box.space.gold:drop()
        end

        if box.space.all_keys then
            box.space.all_keys:drop()
        end
    end)
end)

g_mvcc.test_count_auto = function()
    g_mvcc.server:exec(function()
        -- The test space with fast offset PK.
        local s_test = box.schema.space.create('test')
        s_test:create_index('pk', {fast_offset = true,
                                   parts = {{1, 'unsigned'}, {2, 'unsigned'}}})

        -- The reference space with regular PK.
        local s_gold = box.schema.space.create('gold')
        s_gold:create_index('pk', {fast_offset = false,
                                   parts = {{1, 'unsigned'}, {2, 'unsigned'}}})

        -- The space with regular PK containing all tested keys.
        local s_all_keys = box.schema.space.create('all_keys')
        s_all_keys:create_index('pk', {fast_offset = false, parts = {
                                       {1, 'unsigned'}, {2, 'unsigned'}}})


        -- Create a space to make tested transactions writing - only writing
        -- transactions can cause conflicts with aborts.
        box.schema.space.create('make_conflicting_writer')
        box.space.make_conflicting_writer:create_index('pk', {sequence = true})

        local all_iterators = {'lt', 'le', 'req', 'eq', 'ge', 'gt'}
        local existing_keys = {}
        local unexisting_keys = {}
        local all_keys = {}
        local test_keys = {}

        -- Prepare proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
        local tx = txn_proxy.new()
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()

        -- Proxy helpers.
        local conflict = {{error = "Transaction has been aborted by conflict"}}
        local success = ''

        -- Stringify a table (key or tuple) to use in lua code string.
        -- E. g. array of 2 elements is transformed into "{1, 2}" string.
        local function to_lua_code(table)
            -- Create a raw table without metatables.
            local raw_table = {}
            for k, v in pairs(table) do
                raw_table[k] = v
            end
            return require('luatest.pp').tostring(raw_table)
        end

        -- Create a function that checks if a given table (tuple or key) exists
        -- in the given table. The tuples are matched by their representation in
        -- lua code string (see the to_lua_code function for details).
        local function to_contains(table)
            local contained_values = {}
            for _, tuple in pairs(table) do
                contained_values[to_lua_code(tuple)] = true
            end
            return function(tuple)
                return contained_values[to_lua_code(tuple)] == true
            end
        end

        -- Check if count on sk_fast index with given key and iterator gives the
        -- expected result for the given transaction.
        local function check(tx, it, key, expected_count, file, line)
            -- The location of the callee.
            local file = file or debug.getinfo(2,'S').source
            local line = line or debug.getinfo(2, 'l').currentline

            -- The stringified key.
            local key = to_lua_code(key)

            local code = string.format('box.space.test.index.pk:count(%s, ' ..
                                       '{iterator = "%s"})', key, it)

            local comment = string.format('\nkey: %s,\niterator: %s,' ..
                                          '\nfile: %s,\nline: %d,',
                                          key, it, file, line)

            local ok, res = pcall(tx, code)
            t.assert(ok, comment)
            t.assert_equals(res, {expected_count}, comment)
        end

        -- Make the tx1 open a transaction and count by given it and key,
        -- then make the tx2 insert/delete (op) the given tuple,
        -- then make the tx1 writing to make it abort on conflict,
        -- then make the tx1 commit its transaction and expect tx1_result.
        --
        -- The tuple inserted/deleted by tx2 is cleaned up.
        -- The make_conflicting_writer space is updated but not restored.
        local function count_do(tx1, tx2, it, key, expected_count, op, tuple,
                                tx1_result)
            assert(op == 'insert' or op == 'delete')

            local old_len = s_test:len()
            local tuple_existed = s_test:count(tuple) ~= 0

            -- The location of the callee.
            local file = debug.getinfo(2,'S').source
            local line = debug.getinfo(2, 'l').currentline

            tx1:begin()
            check(tx1, it, key, expected_count, file, line);
            tx2(string.format('box.space.test:%s(%s)', op, to_lua_code(tuple)))
            tx1('box.space.make_conflicting_writer:insert({nil})')
            t.assert_equals(tx1:commit(), tx1_result, string.format(
                            '\nkey: %s\niterator: %s\noperation: %s' ..
                            '\ntuple: %s\nfile: %s,\nline: %s',
                            to_lua_code(key), it, op, to_lua_code(tuple),
                            file, line))

            -- Cleanup.
            local tuple_exists = s_test:count(tuple) ~= 0
            if op == 'insert' and not tuple_existed and tuple_exists then
                s_test:delete(tuple)
            elseif op == 'delete' and tuple_existed and not tuple_exists then
                s_test:insert(tuple)
            end
            t.assert_equals(s_test:len(), old_len)
        end

        -- Some keys are defined to exist in the space, others - aren't. This
        -- is useful for testing (one knows what can be inserted or deleted).
        local function to_exist(i)
            return i % 2 == 1 -- 1, 3, 5 exist, 0, 2, 4, 6 - don't.
        end

        -- Check if the local tables are consistent with space contents.
        local function check_spaces()
            t.assert_equals(s_test:len(), #existing_keys)
            t.assert_equals(s_test:select(), existing_keys)
            t.assert_equals(s_gold:len(), #existing_keys)
            t.assert_equals(s_gold:select(), existing_keys)
            t.assert_equals(s_all_keys:len(), #all_keys)
            t.assert_equals(s_all_keys:select(), all_keys)
        end

        -- Generate key lists.
        for i = 0, 6 do
            for j = 0, 6 do
                if not to_exist(i) or not to_exist(j) then
                    table.insert(unexisting_keys, {i, j})
                else
                    table.insert(existing_keys, {i, j})
                end
                table.insert(all_keys, {i, j})
                table.insert(test_keys, {i, j})
            end
            table.insert(test_keys, {i})
        end
        table.insert(test_keys, {})

        -- Insert the keys to exist.
        for _, key in pairs(existing_keys) do
            s_test:insert(key)
            s_gold:insert(key)
        end

        -- Insert all keys into the corresponding space.
        for _, key in pairs(all_keys) do
            s_all_keys:insert(key)
        end

        check_spaces()

        -- No conflict with itself on any operation.
        for _, it in pairs(all_iterators) do
            for _, key in pairs(all_keys) do
                local expected_count = s_gold:count(key, {iterator = it})
                for _, tuple in pairs(all_keys) do
                    -- The tuple may or may not match with the key and iterator.
                    -- The tuple may or may not exist in the space.
                    -- The operation may be insert, replace or delete.
                    -- The tx must have no conflict with itself in any case.
                    for _, op in pairs({'insert', 'delete'}) do
                        count_do(tx, tx, it, key, expected_count,
                                 op, tuple, success)
                    end
                end
            end
        end
        check_spaces()

        -- No conflict (count by key & replace any key).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local existing_matching = s_gold:select(key, {iterator = it})
                local expected_count = #existing_matching
                for _, tuple in pairs(existing_keys) do
                    count_do(tx1, tx2, it, key, expected_count,
                             'insert', tuple, success)
                end
            end
        end
        check_spaces()

        -- Conflict (count by key & insert matching).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local expected_count = s_gold:count(key, {iterator = it})
                local all_matching = s_all_keys:select(key, {iterator = it})
                for _, tuple in pairs(all_matching) do
                    -- Only check unexisting, so we insert a new tuple.
                    if s_gold:count(tuple) == 0 then
                        count_do(tx1, tx2, it, key, expected_count,
                                 'insert', tuple, conflict)
                    end
                end
            end
        end
        check_spaces()

        -- Conflict (count by key & delete matching existing).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local existing_matching = s_gold:select(key, {iterator = it})
                local expected_count = #existing_matching
                for _, tuple in pairs(existing_matching) do
                    count_do(tx1, tx2, it, key, expected_count,
                             'delete', tuple, conflict)
                end
            end
        end
        check_spaces()

        -- No conflict (count by key & delete matching unexisting).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local expected_count = s_gold:count(key, {iterator = it})
                local all_matching = s_all_keys:select(key, {iterator = it})
                local matches = to_contains(all_matching)
                local exists = to_contains(existing_keys)
                for _, tuple in pairs(all_keys) do
                    if matches(tuple) and not exists(tuple) then
                        count_do(tx1, tx2, it, key, expected_count,
                                 'delete', tuple, success)
                    end
                end
            end
        end
        check_spaces()

        -- No conflict (count by key & insert not matching).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local expected_count = s_gold:count(key, {iterator = it})
                local all_matching = s_all_keys:select(key, {iterator = it})
                local matches = to_contains(all_matching)
                local exists = to_contains(existing_keys)
                for _, tuple in pairs(all_keys) do
                    if not matches(tuple) and not exists(tuple) then
                        count_do(tx1, tx2, it, key, expected_count,
                                 'insert', tuple, success)
                    end
                end
            end
        end
        check_spaces()

        -- No conflict (count by key & delete not matching).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local expected_count = s_gold:count(key, {iterator = it})
                local all_matching = s_all_keys:select(key, {iterator = it})
                local matches = to_contains(all_matching)
                for _, tuple in pairs(all_keys) do
                    if not matches(tuple) then
                        -- The tuple might not exist, no conflict anyways.
                        count_do(tx1, tx2, it, key, expected_count,
                                 'insert', tuple, success)
                    end
                end
            end
        end
        check_spaces()
    end)
end

g_perf.test_count = function()
    g_perf.server:exec(function()
        -- This is a potentially long lasting test (~10s on 3.4GHz Zen 3).
        require('fiber').set_max_slice(30)

        -- A space with two indexes to compare.
        local s = box.schema.space.create('test')
        local key_fast = s:create_index('key_fast', {fast_offset = true})
        local key_slow = s:create_index('key_slow', {fast_offset = false})

        -- Fill the space.
        local count = 1000000
        for i = 1, count do
            s:insert({i})
        end

        -- The test function.
        local function test(key)
            t.assert_equals(key:count({0}, {iterator = 'ge'}), count)
        end

        -- Measure the time to calculate the index:count().
        local clock = require('clock')
        local time_fast = clock.bench(test, key_fast)
        local time_slow = clock.bench(test, key_slow)

        -- The fast one should be visibly faster.
        t.assert_lt(time_fast[1], time_slow[1] / 10)

        s:drop()
    end)
end
