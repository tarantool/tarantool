local server = require('luatest.server')
local t = require('luatest')

local g_generic = t.group('gh-8204-generic')
local g_mvcc = t.group('gh-8204-mvcc')

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

for _, g in pairs({g_generic, g_mvcc}) do
    g.after_all(function()
        g.server:drop()
    end)
end

g_generic.after_each(function()
    g_generic.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

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

g_generic.test_count = function()
    g_generic.server:exec(function()
        -- A space with a secondary key (so we can check nulls).
        local s = box.schema.space.create('test')
        s:create_index('pk')
        local sk = s:create_index('sk',
                                  {parts = {{2, 'uint64', is_nullable = true},
                                            {3, 'uint64', is_nullable = true}}})

        -- A helper function for verbose assertion using pretty printer.
        local function check(it, key, expected_count)
            local pp = require('luatest.pp')

            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
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

g_mvcc.test_count = function()
    g_mvcc.server:exec(function()
        -- The test space with fast offset PK.
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})

        -- Create a space to make tested transactions writing - only writing
        -- transactions can cause conflicts with aborts.
        box.schema.space.create('make_conflicting_writer')
        box.space.make_conflicting_writer:create_index('pk', {sequence = true})

        local kd = require('key_def').new(s.index.pk.parts)

        local all_iterators = {'lt', 'le', 'req', 'eq', 'ge', 'gt'}
        local existing_keys = {}
        local unexisting_keys = {}
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

        -- Check if count on sk_fast index with given key and iterator gives the
        -- expected result for the given transaction.
        local function check(tx, it, key, expected_count, file, line)
            -- The location of the callee.
            local file = file or debug.getinfo(2, 'S').source
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
        -- then make the tx2 insert/replace/delete (op) the given tuple,
        -- then make the tx1 writing to make it abort on conflict,
        -- then make the tx1 commit its transaction and expect tx1_result.
        --
        -- The tuple inserted/deleted by tx2 is cleaned up.
        -- The make_conflicting_writer space is updated but not restored.
        local function count_do(tx1, tx2, it, key, expected_count, op, tuple,
                                tx1_result)
            assert(op == 'insert' or op == 'delete' or op == 'replace')

            local old_len = s:len()
            local tuple_existed = s:count(tuple) ~= 0

            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline

            local key_str = to_lua_code(key)
            local tuple_str = to_lua_code(tuple)

            local tx2_command = string.format('box.space.test:%s(%s)',
                                              op, tuple_str)

            local comment = string.format('\nkey: %s\niterator: %s' ..
                                          '\noperation: %s\ntuple: %s' ..
                                          '\nfile: %s,\nline: %s', key_str,
                                          it, op, tuple_str, file, line)

            -- Remove past stories cause they cause unconditional conflicts,
            -- whereas future statements only conflict with count if they
            -- insert a new matching tuple or delete a counted one.
            box.internal.memtx_tx_gc(100)

            -- Make the tx1 start a transaction and count.
            tx1:begin()
            check(tx1, it, key, expected_count, file, line);

            -- Make the tx2 perform the op.
            t.assert_equals(tx2(tx2_command), {tuple}, comment)

            -- Make the tx1 writing to abort on conflict.
            tx1('box.space.make_conflicting_writer:insert({nil})')

            -- Try to commit the tx1.
            t.assert_equals(tx1:commit(), tx1_result, comment)

            -- Cleanup.
            local tuple_exists = s:count(tuple) ~= 0
            if op == 'insert' and not tuple_existed and tuple_exists then
                s:delete(tuple)
            elseif op == 'delete' and tuple_existed and not tuple_exists then
                s:insert(tuple)
            end
            t.assert_equals(s:len(), old_len, comment)
        end

        -- Check if a tuple matches to the given iterator type and key.
        local function tuple_matches(tuple, it, key)
            -- An empty key matches to anything.
            if #key == 0 then
                return true
            end

            local lt_matches = it == 'lt' or it == 'le'
            local eq_matches = it == 'le' or it == 'ge' or
                               it == 'eq' or it == 'req'
            local gt_matches = it == 'ge' or it == 'gt'

            local cmp = kd:compare_with_key(tuple, key)
            return (cmp == 0 and eq_matches) or
                   (cmp < 0 and lt_matches) or
                   (cmp > 0 and gt_matches)
        end

        -- Simple manual count implementation.
        local function count_matching(t, it, key)
            local result = 0
            for _, tuple in pairs(t) do
                if tuple_matches(tuple, it, key) then
                    result = result + 1
                end
            end
            return result
        end

        -- Check for consistency of count with the given key and iterator in the
        -- given transaction: first performs a count, and then performs inserts
        -- and deletes of keys and checks if the count result remains the same.
        --
        -- If the transaction is nil, starts and commits a new one.
        local function check_consistency(tx_arg, it, key, expected_count)
            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline

            local old_len = s:len()
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

        -- Some keys are defined to exist in the space, others - aren't. This
        -- is useful for testing (one knows what can be inserted or deleted).
        local function to_exist(i)
            return i % 2 == 1 -- 1, 3, 5 exist, 0, 2, 4, 6 - don't.
        end

        -- Check if the local tables are consistent with space contents.
        local function check_space()
            t.assert_equals(s:len(), #existing_keys)
            t.assert_equals(s:select(), existing_keys)
        end

        -- Generate key lists.
        for i = 0, 6 do
            for j = 0, 6 do
                if to_exist(i) or j == i then
                    if not to_exist(i) or not to_exist(j) then
                        table.insert(unexisting_keys, {i, j})
                    else
                        table.insert(existing_keys, {i, j})
                    end
                    table.insert(test_keys, {i, j})
                end
            end
            table.insert(test_keys, {i})
        end
        table.insert(test_keys, {})

        -- Insert the keys to exist.
        for _, key in pairs(existing_keys) do
            s:insert(key)
        end
        check_space()

        -- No conflict (count by key & replace any key).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local expect = count_matching(existing_keys, it, key)
                for _, tuple in pairs(existing_keys) do
                    count_do(tx1, tx2, it, key, expect,
                             'replace', tuple, success)
                end
            end
        end
        check_space()

        -- Conflict (count by key & insert matching).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local expect = count_matching(existing_keys, it, key)
                for _, tuple in pairs(unexisting_keys) do
                    if tuple_matches(tuple, it, key) then
                        count_do(tx1, tx2, it, key, expect,
                                 'insert', tuple, conflict)
                    end
                end
            end
        end
        check_space()

        -- Conflict (count by key & delete matching).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local expect = count_matching(existing_keys, it, key)
                for _, tuple in pairs(existing_keys) do
                    if tuple_matches(tuple, it, key) then
                        count_do(tx1, tx2, it, key, expect,
                                 'delete', tuple, conflict)
                    end
                end
            end
        end
        check_space()

        -- No conflict (count by key & insert not matching).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local expect = count_matching(existing_keys, it, key)
                for _, tuple in pairs(unexisting_keys) do
                    if not tuple_matches(tuple, it, key) then
                        count_do(tx1, tx2, it, key, expect,
                                 'insert', tuple, success)
                    end
                end
            end
        end
        check_space()

        -- No conflict (count by key & delete not matching).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local expect = count_matching(existing_keys, it, key)
                for _, tuple in pairs(existing_keys) do
                    if not tuple_matches(tuple, it, key) then
                        count_do(tx1, tx2, it, key, expect,
                                 'delete', tuple, success)
                    end
                end
            end
        end
        check_space()

        -- Consistency in the read view.
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local expect = count_matching(existing_keys, it, key)
                check_consistency(nil, it, key, expect)
            end
        end
        check_space()

        -- Consistency in the read view (in a single transaction).
        tx:begin()
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local expect = count_matching(existing_keys, it, key)
                check_consistency(tx, it, key, expect)
            end
        end
        t.assert_equals(tx:commit(), success)
        check_space()
    end)
end

g_mvcc.test_past_history = function()
    g_mvcc.server:exec(function()
        -- The test space.
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})

        -- Prepare proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
        local tx1 = txn_proxy.new()
        local tx2 = txn_proxy.new()

        tx1:begin()
        tx2:begin()
        tx1('box.space.test:replace{1, 0}')

        -- No prepared tuples - count must return 0.
        local count = tx2('return box.space.test:count{1, 0}')[1]
        t.assert_equals(count, 0)

        -- Commit writer.
        tx1:commit()

        -- Count again - the same value (zero) must be returned.
        count = tx2('return box.space.test:count{1, 0}')[1]
        t.assert_equals(count, 0)
        tx2:commit()

        -- Check if insert actually happened.
        t.assert_equals(box.space.test:select{}, {{1, 0}})
    end)
end
