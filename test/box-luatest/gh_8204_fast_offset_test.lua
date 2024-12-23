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

g_generic.test_offset = function()
    g_generic.server:exec(function()
        -- A space with a secondary key (so we can check nulls).
        local s = box.schema.space.create('test')
        s:create_index('pk')

        -- The tested index.
        local sk = s:create_index('sk',
                                  {parts = {{2, 'uint64', is_nullable = true},
                                            {3, 'uint64', is_nullable = true}}})

        -- The test data.

        local existing_tuples = {
            {1, 1, 1},
            {2, 1, 3},
            {3, 3, 1},
            {4, 3, 2},
            {5, 5, 1},
            {6, 5, 3},
        }

        local test_keys = {
            {},
            {box.NULL}, {box.NULL, box.NULL}, {box.NULL, 0},
            {box.NULL, 1}, {box.NULL, 2}, {box.NULL, 3}, {box.NULL, 4},
            {0}, {0, box.NULL}, {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4},
            {1}, {1, box.NULL}, {1, 0}, {1, 1}, {1, 2}, {1, 3}, {1, 4},
            {2}, {2, box.NULL}, {2, 0}, {2, 1}, {2, 2}, {2, 3}, {2, 4},
            {3}, {3, box.NULL}, {3, 0}, {3, 1}, {3, 2}, {3, 3}, {3, 4},
            {4}, {4, box.NULL}, {4, 0}, {4, 1}, {4, 2}, {4, 3}, {4, 4},
            {5}, {5, box.NULL}, {5, 0}, {5, 1}, {5, 2}, {5, 3}, {5, 4},
            {6}, {6, box.NULL}, {6, 0}, {6, 1}, {6, 2}, {6, 3}, {6, 4},
        }

        local test_offsets = {0, 3, 10}

        local all_iterators = {'lt', 'le', 'eq', 'req', 'ge', 'gt', 'all'}

        -- A helper function for verbose assertion using pretty printer.
        local function check(it, key, offset, expect)
            local pp = require('luatest.pp')

            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline

            -- The stringified key.
            local key_str = pp.tostring(key)

            local comment = string.format(
                '\nkey: %s,\noffset = %d,\niterator: %s,\nfile: %s,' ..
                '\nline: %d,', key_str, offset, it, file, line)

            local opts = {iterator = it, offset = offset}

            local result = sk:select(key, opts)
            t.assert_equals(result, expect, comment)

            local pairs_result = {}
            for _, tuple in sk:pairs(key, opts) do
                table.insert(pairs_result, tuple)
            end
            t.assert_equals(pairs_result, expect, comment)
        end

        -- Test the empty space.
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                for _, offset in pairs(test_offsets) do
                    check(it, key, offset, {})
                end
            end
        end

        -- Fill the space.
        for _, tuple in pairs(existing_tuples) do
            s:insert(tuple)
        end

        -- Test the non-empty space.
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local selected = sk:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    check(it, key, offset, expect)
                end
            end
        end
    end)
end

g_generic.test_offset_of = function()
    g_generic.server:exec(function()
        -- Create and fill the space.
        local s = box.schema.space.create('test')

        -- A test wrapper to fit cases in single lines.
        local function check(key, it, expect)
            local pp = require('luatest.pp')

            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline

            -- The stringified key.
            local key_str = pp.tostring(key)

            t.assert_equals(s:offset_of(key, {iterator = it}), expect,
                            string.format('\nkey: %s,\niterator: %s,' ..
                                          '\nfile: %s,\nline: %d,',
                                          key_str, it, file, line))
        end

        -- Test a space without indexes. This is not right since we have no
        -- indexes and thus no way to check if the given key is valid, but
        -- that's the way it works, so it has to be tested.
        for _, it in pairs({'lt', 'le', 'req', 'eq', 'ge', 'gt', 'np', 'pp'}) do
            check({}, it, 0)
            check({37}, it, 0)
            check({"string"}, it, 0)
            check({"multi", "part"}, it, 0)
        end

        -- Create and fill an index of unsigned integers.
        s:create_index('pk')
        s:insert({1})
        s:insert({3})

        -- iterator = GE
        check({0}, 'ge', 0) -- [<1>, 3]
        check({1}, 'ge', 0) -- [<1>, 3]
        check({2}, 'ge', 1) -- [1, <3>]
        check({3}, 'ge', 1) -- [1, <3>]
        check({4}, 'ge', 2) -- [1, 3, <...>]

        -- iterator = GT
        check({0}, 'gt', 0) -- [<1>, 3]
        check({1}, 'gt', 1) -- [1, <3>]
        check({2}, 'gt', 1) -- [1, <3>]
        check({3}, 'gt', 2) -- [1, 3, <...>]
        check({4}, 'gt', 2) -- [1, 3, <...>]

        -- iterator = LE
        check({0}, 'le', 2) -- [3, 1, <...>]
        check({1}, 'le', 1) -- [3, <1>]
        check({2}, 'le', 1) -- [3, <1>]
        check({3}, 'le', 0) -- [<3>, 1]
        check({4}, 'le', 0) -- [<3>, 1]

        -- iterator = LT
        check({0}, 'lt', 2) -- [3, 1, <...>]
        check({1}, 'lt', 2) -- [3, 1, <...>]
        check({2}, 'lt', 1) -- [3, <1>]
        check({3}, 'lt', 1) -- [3, <1>]
        check({4}, 'lt', 0) -- [<3>, 1]

        -- iterator = EQ
        check({0}, 'eq', 0) -- [<0>, 1, 3]
        check({1}, 'eq', 0) -- [<1>, 3]
        check({2}, 'eq', 1) -- [1, <2>, 3]
        check({3}, 'eq', 1) -- [1, <3>]
        check({4}, 'eq', 2) -- [1, 3, <4>]

        -- iterator = REQ
        check({0}, 'req', 2) -- [3, 1, <0>]
        check({1}, 'req', 1) -- [3, <1>]
        check({2}, 'req', 1) -- [3, <2>, 1]
        check({3}, 'req', 0) -- [<3>, 1]
        check({4}, 'req', 0) -- [<4>, 3, 1]

        -- Create and fill an index of strings.
        s.index.pk:drop()
        s:create_index('pk', {parts = {1, 'string'}})
        s:insert({'b'})
        s:insert({'bb'})
        s:insert({'bc'})
        s:insert({'c'})
        s:insert({'cc'})

        -- iterator = NP
        check({'a'},  'np', 0) -- [<b>, bb, bc, c, cc]
        check({'ba'}, 'np', 1) -- [b, <bb>, bc, c, cc]
        check({'bb'}, 'np', 2) -- [b, bb, <bc>, c, cc]
        check({'b'},  'np', 3) -- [b, bb, bc, <c>, cc]
        check({'ca'}, 'np', 4) -- [b, bb, bc, c, <cc>]
        check({'c'},  'np', 5) -- [b, bb, bc, c, cc, <...>]

        -- iterator = PP
        check({'b'},  'pp', 5) -- [cc, c, bc, bb, b, <...>]
        check({'bb'}, 'pp', 4) -- [cc, c, bc, bb, <b>]
        check({'bc'}, 'pp', 3) -- [cc, c, bc, <bb>, b]
        check({'bd'}, 'pp', 2) -- [cc, c, <bc>, bb, b]
        check({'cc'}, 'pp', 1) -- [cc, <c>, bc, bb, b]
        check({'cd'}, 'pp', 0) -- [<cc>, c, bc, bb, b]

        -- Create and fill a multipart index.
        s.index.pk:drop()
        s:create_index('pk', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})
        s:insert({1, 1})
        s:insert({1, 2})
        s:insert({2, 1})

        check({1}, 'eq',  0) -- [<{1, 1}>, {1, 2}, {2, 1}]
        check({1}, 'gt',  2) -- [{1, 1}, {1, 2}, <{2, 1}>]
        check({1}, 'ge',  0) -- [<{1, 1}>, {1, 2}, {2, 1}]
        check({1}, 'req', 1) -- [{2, 1}, <{1, 2}>, {1, 1}]
        check({1}, 'lt',  3) -- [{2, 1}, {1, 2}, {1, 1}, <...>]
        check({1}, 'le',  1) -- [{2, 1}, <{1, 2}>, {1, 1}]
    end)
end

-- Checks the 'offset_of' function parameters.
g_generic.test_offset_of_params = function()
    g_generic.server:exec(function()
        -- Create and fill a space.
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})
        s:insert({1, 1})
        s:insert({1, 2})
        s:insert({2, 1})
        s:insert({2, 2})
        s:insert({3, 1})
        s:insert({3, 2})

        -- No arguments.
        t.assert_equals(s:offset_of(), 0)

        -- Empty key.
        t.assert_equals(s:offset_of({}), 0)

        -- Numeric key.
        t.assert_equals(s:offset_of(2), 2)

        -- Regular key.
        t.assert_equals(s:offset_of({2}), 2)

        -- Empty opts.
        t.assert_equals(s:offset_of({2}, {}), 2)

        -- String iterator.
        t.assert_equals(s:offset_of({2}, {iterator = 'eq'}), 2)

        -- Number iterator.
        t.assert_equals(s:offset_of({2}, {iterator = box.index.EQ}), 2)

        -- Invalid iterator.
        t.assert_error_msg_contains('Unknown iterator type',
                                    s.offset_of, s, {2}, {iterator = "bad"})

        -- Invalid iterator type.
        t.assert_error_msg_contains('Unknown iterator type',
                                    s.offset_of, s, {2}, {iterator = true})

        -- String opts.
        t.assert_equals(s:offset_of({2}, 'eq'), 2)

        -- Number opts.
        t.assert_equals(s:offset_of({2}, box.index.EQ), 2)

        -- Invalid opts.
        t.assert_error_msg_contains('Unknown iterator type',
                                    s.offset_of, s, {2}, 'bad')

        -- Invalid type opts.
        t.assert_error_msg_contains('Unknown iterator type',
                                    s.offset_of, s, {2}, true)
    end)
end

g_generic.after_test('test_offset_of_params', function()
    g_generic.server:exec(function()
        if box.space.test then
            box.space.test:drop()
        end
    end)
end)

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

g_mvcc.test_offset = function()
    g_mvcc.server:exec(function()
        -- The test space.
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})

        -- Create a space to make tested transactions writing - only writing
        -- transactions can cause conflicts with aborts.
        box.schema.space.create('make_conflicting_writer')
        box.space.make_conflicting_writer:create_index('pk', {sequence = true})

        local kd = require('key_def').new(s.index.pk.parts)

        local all_iterators = {'lt', 'le', 'req', 'eq', 'ge', 'gt'}
        local iterator_is_reverse = {lt = true, le = true, req = true}
        local existing_keys = {}
        local unexisting_keys = {}
        local all_keys = {}
        local test_keys = {}
        local test_offsets = {0, 3, 100}

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

        -- Some keys are defined to exist in the space, others - aren't. This
        -- is useful for testing (one knows what can be inserted or deleted).
        local function to_exist(i)
            return i % 2 == 1 -- 1, 3, 5 exist, 0, 2, 4, 6 - don't.
        end

        -- Check if the local tables are consistent with space contents.
        local function check_spaces()
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
                    table.insert(all_keys, {i, j})
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
        check_spaces()

        -- Check if select in the test space with given key and iterator gives
        -- the expected result.
        local function check(tx, it, key, offset, expect, file, line)
            -- The location of the callee.
            local file = file or debug.getinfo(2, 'S').source
            local line = line or debug.getinfo(2, 'l').currentline

            -- The stringified key.
            local key_str = to_lua_code(key)

            local code = string.format('box.space.test:select(%s, ' ..
                                       '{iterator = "%s", offset = %d})',
                                       key_str, it, offset)

            local comment = string.format(
                '\nkey: %s,\niterator: %s,\noffset = %d\nfile: %s,' ..
                '\nline: %d,', key_str, it, offset, file, line)

            box.space.test.index.pk:len()
            local ok, res = pcall(tx, code)
            box.space.test.index.pk:bsize()
            t.assert(ok, comment)
            t.assert_equals(res, {expect}, comment)
        end

        -- Make the tx1 open a transaction and select with iter, key and offset.
        -- then make the tx2 insert/replace/delete (op) the given tuple,
        -- then make the tx1 writing to make it abort on conflict,
        -- then make the tx1 commit its transaction and expect tx1_result.
        --
        -- The tuple inserted/deleted by tx2 is cleaned up.
        -- The make_conflicting_writer space is updated but not restored.
        local function select_do(it, key, offset, expect, op, tuple, tx1_result)
            assert(op == 'insert' or op == 'replace' or op == 'delete')

            local old_len = s:len()
            local tuple_existed = s:count(tuple) ~= 0

            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline

            local key_str = to_lua_code(key)
            local tuple_str = to_lua_code(tuple)

            local tx2_command = string.format('return box.space.test:%s(%s)',
                                              op, tuple_str)

            local comment = string.format('\nkey: %s\niterator: %s\noffset: ' ..
                                          '%d\noperation: %s\ntuple: %s\n' ..
                                          'file: %s,\nline: %s', key_str, it,
                                          offset, op, tuple_str, file, line)

            -- Remove past stories cause they cause unconditional conflicts,
            -- whereas future statements only conflict with count if they
            -- insert a new matching tuple or delete a counted one.
            box.internal.memtx_tx_gc(100)

            -- Make the tx1 start a transaction and count.
            tx1:begin()
            check(tx1, it, key, offset, expect, file, line);

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

        -- Check for consistency of select with key, iterator and offset in the
        -- given transaction: first performs a select, and then performs various
        -- inserts, replaces and deletes of keys and checks if the select result
        -- remains the same.
        --
        -- If the transaction is nil, starts and commits a new one.
        local function check_consistency(tx_arg, it, key, offset, expect)
            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline

            local existing_keys = s:select()
            local old_len = #existing_keys
            local tx = tx_arg or txn_proxy.new()

            -- Start a transaction manually if no passed.
            if tx_arg == nil then
                tx:begin()
            end

            check(tx, it, key, offset, expect, file, line)
            for _, new_key in pairs(unexisting_keys) do
                s:insert(new_key)
                check(tx, it, key, offset, expect, file, line)
            end
            for _, old_key in pairs(existing_keys) do
                s:delete(old_key)
                check(tx, it, key, offset, expect, file, line)
            end
            for _, old_key in pairs(unexisting_keys) do
                s:delete(old_key)
                check(tx, it, key, offset, expect, file, line)
            end
            for _, new_key in pairs(existing_keys) do
                s:insert(new_key)
                check(tx, it, key, offset, expect, file, line)
            end

            -- Autocommit if no transaction passed.
            if tx_arg == nil then
                t.assert_equals(tx:commit(), success)
            end

            t.assert_equals(s:len(), old_len)
            t.assert_equals(s:select(), existing_keys)
        end

        -- Conflict (select & replace first).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local selected = s:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    local first = selected[offset + 1]
                    if first ~= nil then
                        select_do(it, key, offset, expect,
                                  'replace', first, conflict)
                    end
                end
            end
        end

        -- Conflict (select & delete first).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local selected = s:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    local first = selected[offset + 1]
                    if first ~= nil then
                        select_do(it, key, offset, expect,
                                  'delete', first, conflict)
                    end
                end
            end
        end

        -- No conflict (select & replace skipped).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local selected = s:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    local skipped = {unpack(selected, 1, offset)}
                    for _, tuple in pairs(skipped) do
                        select_do(it, key, offset, expect,
                                  'replace', tuple, success)
                    end
                end
            end
        end
        check_spaces()

        -- Conflict (select & delete skipped).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local selected = s:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    local skipped = {unpack(selected, 1, offset)}
                    for _, tuple in pairs(skipped) do
                        select_do(it, key, offset, expect,
                                  'delete', tuple, conflict)
                    end
                end
            end
        end
        check_spaces()

        -- Conflict (select & insert potentially skipped).
        for _, it in pairs(all_iterators) do
            local dir = iterator_is_reverse[it] and -1 or 1
            for _, key in pairs(test_keys) do
                local selected = s:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    local first = selected[offset + 1]
                    for _, tuple in pairs(unexisting_keys) do
                        local matches = tuple_matches(tuple, it, key)
                        local is_before_first = first == nil or
                            kd:compare(tuple, first) * dir >= 0
                        if matches and is_before_first then
                            select_do(it, key, offset, expect,
                                      'insert', tuple, conflict)
                        end
                    end
                end
            end
        end

        -- No conflict (select & replace not matching).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local selected = s:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    for _, tuple in pairs(existing_keys) do
                        if not tuple_matches(tuple, it, key) then
                            select_do(it, key, offset, expect,
                                      'replace', tuple, success)
                        end
                    end
                end
            end
        end
        check_spaces()

        -- No conflict (select & delete not matching).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local selected = s:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    for _, tuple in pairs(existing_keys) do
                        if not tuple_matches(tuple, it, key) then
                            select_do(it, key, offset, expect,
                                      'delete', tuple, success)
                        end
                    end
                end
            end
        end
        check_spaces()

        -- No conflict (select & insert not matching).
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local selected = s:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    for _, tuple in pairs(unexisting_keys) do
                        if not tuple_matches(tuple, it, key) then
                            select_do(it, key, offset, expect,
                                      'insert', tuple, success)
                        end
                    end
                end
            end
        end
        check_spaces()

        -- Consistency in the read view.
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local selected = s:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    check_consistency(nil, it, key, offset, expect)
                end
            end
        end
        check_spaces()

        -- Consistency in the read view (in a single transaction).
        tx:begin()
        for _, it in pairs(all_iterators) do
            for _, key in pairs(test_keys) do
                local selected = s:select(key, {iterator = it})
                for _, offset in pairs(test_offsets) do
                    local expect = {unpack(selected, offset + 1, #selected)}
                    check_consistency(tx, it, key, offset, expect)
                end
            end
        end
        t.assert_equals(tx:commit(), success)
        check_spaces()
    end)
end
