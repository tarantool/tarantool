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

        -- The test space data.
        local existing_keys = {{1, 1}, {1, 3}, {1, 5},
                               {3, 1}, {3, 3}, {3, 5},
                               {5, 1}, {5, 3}, {5, 5}}

        -- Prepare proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
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

        -- Check if count on the primary kay with given key and iterator gives
        -- the expected result for the given transaction.
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

        -- Check if the local tables are consistent with space contents.
        local function check_space()
            t.assert_equals(s:len(), #existing_keys)
            t.assert_equals(s:select(), existing_keys)
        end

        -- Insert the keys to exist.
        for _, key in pairs(existing_keys) do
            s:insert(key)
        end
        check_space()

        -- No conflict (count by full key & replace any key).
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'replace', {1, 1}, success)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'replace', {1, 5}, success)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'replace', {3, 1}, success)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'replace', {3, 3}, success)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'replace', {5, 1}, success)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'replace', {5, 5}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'replace', {1, 1}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'replace', {1, 5}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'replace', {3, 3}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'replace', {3, 5}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'replace', {5, 3}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'replace', {5, 5}, success)
        for _, it in pairs({'eq', 'req'}) do
            count_do(tx1, tx2, it, {3, 3}, 1, 'replace', {1, 1}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'replace', {1, 5}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'replace', {3, 1}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'replace', {3, 3}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'replace', {3, 5}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'replace', {5, 3}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'replace', {5, 5}, success)
        end
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'replace', {1, 1}, success)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'replace', {1, 5}, success)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'replace', {3, 1}, success)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'replace', {3, 3}, success)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'replace', {5, 1}, success)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'replace', {5, 5}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'replace', {1, 1}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'replace', {1, 5}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'replace', {3, 3}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'replace', {3, 5}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'replace', {5, 3}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'replace', {5, 5}, success)
        check_space()

        -- No conflict (count by partial key & replace any key).
        count_do(tx1, tx2, 'lt', {3}, 3, 'replace', {1, 1}, success)
        count_do(tx1, tx2, 'lt', {3}, 3, 'replace', {1, 3}, success)
        count_do(tx1, tx2, 'lt', {3}, 3, 'replace', {1, 5}, success)
        count_do(tx1, tx2, 'lt', {3}, 3, 'replace', {3, 1}, success)
        count_do(tx1, tx2, 'lt', {3}, 3, 'replace', {5, 1}, success)
        count_do(tx1, tx2, 'lt', {3}, 3, 'replace', {5, 5}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'replace', {1, 1}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'replace', {3, 1}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'replace', {3, 5}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'replace', {5, 1}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'replace', {5, 3}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'replace', {5, 5}, success)
        for _, it in pairs({'eq', 'req'}) do
            count_do(tx1, tx2, it, {3}, 3, 'replace', {1, 1}, success)
            count_do(tx1, tx2, it, {3}, 3, 'replace', {1, 3}, success)
            count_do(tx1, tx2, it, {3}, 3, 'replace', {1, 5}, success)
            count_do(tx1, tx2, it, {3}, 3, 'replace', {3, 1}, success)
            count_do(tx1, tx2, it, {3}, 3, 'replace', {3, 3}, success)
            count_do(tx1, tx2, it, {3}, 3, 'replace', {3, 5}, success)
            count_do(tx1, tx2, it, {3}, 3, 'replace', {5, 1}, success)
            count_do(tx1, tx2, it, {3}, 3, 'replace', {5, 3}, success)
            count_do(tx1, tx2, it, {3}, 3, 'replace', {5, 5}, success)
        end
        count_do(tx1, tx2, 'ge', {3}, 6, 'replace', {1, 1}, success)
        count_do(tx1, tx2, 'ge', {3}, 6, 'replace', {1, 3}, success)
        count_do(tx1, tx2, 'ge', {3}, 6, 'replace', {1, 5}, success)
        count_do(tx1, tx2, 'ge', {3}, 6, 'replace', {3, 1}, success)
        count_do(tx1, tx2, 'ge', {3}, 6, 'replace', {5, 1}, success)
        count_do(tx1, tx2, 'ge', {3}, 6, 'replace', {5, 5}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'replace', {1, 1}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'replace', {3, 1}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'replace', {3, 5}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'replace', {5, 1}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'replace', {5, 3}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'replace', {5, 5}, success)
        check_space()

        -- No conflict (count all & replace any key).
        count_do(tx1, tx2, 'all', {}, 9, 'replace', {1, 1}, success)
        count_do(tx1, tx2, 'all', {}, 9, 'replace', {3, 3}, success)
        count_do(tx1, tx2, 'all', {}, 9, 'replace', {5, 5}, success)
        check_space()

        -- Conflict (count by full key & insert matching).
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'insert', {1, 6}, conflict)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'insert', {3, 2}, conflict)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'insert', {1, 6}, conflict)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'insert', {3, 2}, conflict)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'insert', {3, 4}, conflict)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'insert', {5, 2}, conflict)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'insert', {6, 6}, conflict)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'insert', {3, 4}, conflict)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'insert', {5, 2}, conflict)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'insert', {6, 6}, conflict)
        check_space()

        -- Conflict (count by partial key & insert matching).
        count_do(tx1, tx2, 'lt', {3}, 3, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'lt', {3}, 3, 'insert', {1, 4}, conflict)
        count_do(tx1, tx2, 'lt', {3}, 3, 'insert', {2, 6}, conflict)
        count_do(tx1, tx2, 'le', {3}, 6, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'le', {3}, 6, 'insert', {2, 0}, conflict)
        count_do(tx1, tx2, 'le', {3}, 6, 'insert', {3, 6}, conflict)
        for _, it in pairs({'eq', 'req'}) do
            count_do(tx1, tx2, it, {3}, 3, 'insert', {3, 0}, conflict)
            count_do(tx1, tx2, it, {3}, 3, 'insert', {3, 4}, conflict)
            count_do(tx1, tx2, it, {3}, 3, 'insert', {3, 6}, conflict)
        end
        count_do(tx1, tx2, 'ge', {3}, 6, 'insert', {3, 0}, conflict)
        count_do(tx1, tx2, 'ge', {3}, 6, 'insert', {5, 0}, conflict)
        count_do(tx1, tx2, 'ge', {3}, 6, 'insert', {6, 6}, conflict)
        count_do(tx1, tx2, 'gt', {3}, 3, 'insert', {4, 0}, conflict)
        count_do(tx1, tx2, 'gt', {3}, 3, 'insert', {5, 4}, conflict)
        count_do(tx1, tx2, 'gt', {3}, 3, 'insert', {6, 6}, conflict)
        check_space()

        -- Conflict (count all & insert matching).
        count_do(tx1, tx2, 'all', {}, 9, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'all', {}, 9, 'insert', {3, 4}, conflict)
        count_do(tx1, tx2, 'all', {}, 9, 'insert', {6, 6}, conflict)
        check_space()

        -- Conflict (count full unexisting & insert matching).
        count_do(tx1, tx2, 'lt', {1, 1}, 0, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'lt', {1, 1}, 0, 'insert', {1, 0}, conflict)
        count_do(tx1, tx2, 'le', {1, 0}, 0, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'le', {1, 0}, 0, 'insert', {1, 0}, conflict)
        count_do(tx1, tx2, 'eq', {3, 4}, 0, 'insert', {3, 4}, conflict)
        count_do(tx1, tx2, 'req', {3, 4}, 0, 'insert', {3, 4}, conflict)
        count_do(tx1, tx2, 'ge', {5, 6}, 0, 'insert', {5, 6}, conflict)
        count_do(tx1, tx2, 'ge', {5, 6}, 0, 'insert', {6, 6}, conflict)
        count_do(tx1, tx2, 'gt', {5, 5}, 0, 'insert', {5, 6}, conflict)
        count_do(tx1, tx2, 'gt', {5, 5}, 0, 'insert', {6, 6}, conflict)

        -- Conflict (count partial unexisting & insert matching).
        count_do(tx1, tx2, 'lt', {1}, 0, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'le', {0}, 0, 'insert', {0, 0}, conflict)
        count_do(tx1, tx2, 'eq', {2}, 0, 'insert', {2, 3}, conflict)
        count_do(tx1, tx2, 'req', {2}, 0, 'insert', {2, 3}, conflict)
        count_do(tx1, tx2, 'ge', {6}, 0, 'insert', {6, 3}, conflict)
        count_do(tx1, tx2, 'gt', {5}, 0, 'insert', {6, 3}, conflict)

        -- Conflict (count by full key & delete matching).
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'delete', {1, 1}, conflict)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'delete', {1, 5}, conflict)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'delete', {3, 1}, conflict)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'delete', {1, 1}, conflict)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'delete', {1, 5}, conflict)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'delete', {3, 3}, conflict)
        count_do(tx1, tx2, 'req', {3, 3}, 1, 'delete', {3, 3}, conflict)
        count_do(tx1, tx2, 'eq', {3, 3}, 1, 'delete', {3, 3}, conflict)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'delete', {3, 3}, conflict)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'delete', {5, 1}, conflict)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'delete', {5, 5}, conflict)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'delete', {3, 5}, conflict)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'delete', {5, 3}, conflict)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'delete', {5, 5}, conflict)
        check_space()

        -- Conflict (count by partial key & delete matching).
        count_do(tx1, tx2, 'lt', {3}, 3, 'delete', {1, 1}, conflict)
        count_do(tx1, tx2, 'lt', {3}, 3, 'delete', {1, 3}, conflict)
        count_do(tx1, tx2, 'lt', {3}, 3, 'delete', {1, 5}, conflict)
        count_do(tx1, tx2, 'le', {3}, 6, 'delete', {1, 1}, conflict)
        count_do(tx1, tx2, 'le', {3}, 6, 'delete', {3, 1}, conflict)
        count_do(tx1, tx2, 'le', {3}, 6, 'delete', {3, 5}, conflict)
        for _, it in pairs({'eq', 'req'}) do
            count_do(tx1, tx2, it, {3}, 3, 'delete', {3, 1}, conflict)
            count_do(tx1, tx2, it, {3}, 3, 'delete', {3, 3}, conflict)
            count_do(tx1, tx2, it, {3}, 3, 'delete', {3, 5}, conflict)
        end
        count_do(tx1, tx2, 'ge', {3}, 6, 'delete', {3, 1}, conflict)
        count_do(tx1, tx2, 'ge', {3}, 6, 'delete', {5, 1}, conflict)
        count_do(tx1, tx2, 'ge', {3}, 6, 'delete', {5, 5}, conflict)
        count_do(tx1, tx2, 'gt', {3}, 3, 'delete', {5, 1}, conflict)
        count_do(tx1, tx2, 'gt', {3}, 3, 'delete', {5, 3}, conflict)
        count_do(tx1, tx2, 'gt', {3}, 3, 'delete', {5, 5}, conflict)
        check_space()

        -- Conflict (count all & delete matching).
        count_do(tx1, tx2, 'all', {}, 9, 'delete', {1, 1}, conflict)
        count_do(tx1, tx2, 'all', {}, 9, 'delete', {3, 3}, conflict)
        count_do(tx1, tx2, 'all', {}, 9, 'delete', {5, 5}, conflict)
        check_space()

        -- No conflict (count by full key & insert not matching).
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'insert', {3, 4}, success)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'insert', {5, 2}, success)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'insert', {6, 6}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'insert', {3, 4}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'insert', {5, 2}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'insert', {6, 6}, success)
        for _, it in pairs({'eq', 'req'}) do
            count_do(tx1, tx2, it, {3, 3}, 1, 'insert', {0, 0}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'insert', {3, 4}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'insert', {6, 6}, success)
        end
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'insert', {0, 0}, success)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'insert', {1, 6}, success)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'insert', {3, 2}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'insert', {0, 0}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'insert', {1, 6}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'insert', {3, 2}, success)
        check_space()

        -- No conflict (count by partial key & insert not matching).
        count_do(tx1, tx2, 'lt', {3}, 3, 'insert', {3, 0}, success)
        count_do(tx1, tx2, 'lt', {3}, 3, 'insert', {5, 0}, success)
        count_do(tx1, tx2, 'lt', {3}, 3, 'insert', {6, 6}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'insert', {4, 0}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'insert', {5, 4}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'insert', {6, 6}, success)
        for _, it in pairs({'eq', 'req'}) do
            count_do(tx1, tx2, it, {3}, 3, 'insert', {0, 0}, success)
            count_do(tx1, tx2, it, {3}, 3, 'insert', {1, 4}, success)
            count_do(tx1, tx2, it, {3}, 3, 'insert', {2, 6}, success)
            count_do(tx1, tx2, it, {3}, 3, 'insert', {4, 0}, success)
            count_do(tx1, tx2, it, {3}, 3, 'insert', {5, 4}, success)
            count_do(tx1, tx2, it, {3}, 3, 'insert', {6, 6}, success)
        end
        count_do(tx1, tx2, 'ge', {3}, 6, 'insert', {0, 0}, success)
        count_do(tx1, tx2, 'ge', {3}, 6, 'insert', {1, 4}, success)
        count_do(tx1, tx2, 'ge', {3}, 6, 'insert', {2, 6}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'insert', {0, 0}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'insert', {2, 0}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'insert', {3, 6}, success)
        check_space()

        -- No conflict (count by full key & delete not matching).
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'delete', {3, 3}, success)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'delete', {5, 1}, success)
        count_do(tx1, tx2, 'lt', {3, 3}, 4, 'delete', {5, 5}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'delete', {3, 5}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'delete', {5, 3}, success)
        count_do(tx1, tx2, 'le', {3, 3}, 5, 'delete', {5, 5}, success)
        for _, it in pairs({'eq', 'req'}) do
            count_do(tx1, tx2, it, {3, 3}, 1, 'delete', {1, 1}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'delete', {1, 5}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'delete', {3, 1}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'delete', {3, 5}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'delete', {5, 3}, success)
            count_do(tx1, tx2, it, {3, 3}, 1, 'delete', {5, 5}, success)
        end
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'delete', {1, 1}, success)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'delete', {1, 5}, success)
        count_do(tx1, tx2, 'ge', {3, 3}, 5, 'delete', {3, 1}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'delete', {1, 1}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'delete', {1, 5}, success)
        count_do(tx1, tx2, 'gt', {3, 3}, 4, 'delete', {3, 3}, success)
        check_space()

        -- No conflict (count by partial key & delete not matching).
        count_do(tx1, tx2, 'lt', {3}, 3, 'delete', {3, 1}, success)
        count_do(tx1, tx2, 'lt', {3}, 3, 'delete', {5, 1}, success)
        count_do(tx1, tx2, 'lt', {3}, 3, 'delete', {5, 5}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'delete', {5, 1}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'delete', {5, 3}, success)
        count_do(tx1, tx2, 'le', {3}, 6, 'delete', {5, 5}, success)
        for _, it in pairs({'eq', 'req'}) do
            count_do(tx1, tx2, it, {3}, 3, 'delete', {1, 1}, success)
            count_do(tx1, tx2, it, {3}, 3, 'delete', {1, 3}, success)
            count_do(tx1, tx2, it, {3}, 3, 'delete', {1, 5}, success)
            count_do(tx1, tx2, it, {3}, 3, 'delete', {5, 1}, success)
            count_do(tx1, tx2, it, {3}, 3, 'delete', {5, 3}, success)
            count_do(tx1, tx2, it, {3}, 3, 'delete', {5, 5}, success)
        end
        count_do(tx1, tx2, 'ge', {3}, 6, 'delete', {1, 1}, success)
        count_do(tx1, tx2, 'ge', {3}, 6, 'delete', {1, 3}, success)
        count_do(tx1, tx2, 'ge', {3}, 6, 'delete', {1, 5}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'delete', {1, 1}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'delete', {3, 1}, success)
        count_do(tx1, tx2, 'gt', {3}, 3, 'delete', {3, 5}, success)
        check_space()
    end)
end

g_mvcc.test_count_consistency = function()
    g_mvcc.server:exec(function()
        -- The test space with fast offset PK.
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})

        local existing_keys = {{1, 1}, {1, 3}, {1, 5},
                               {3, 1}, {3, 3}, {3, 5},
                               {5, 1}, {5, 3}, {5, 5}}
        local unexisting_keys = {{1, 0}, {1, 2}, {1, 4}, {1, 6},
                                 {3, 0}, {3, 2}, {3, 4}, {3, 6},
                                 {5, 0}, {5, 2}, {5, 4}, {5, 6},
                                 {0, 0}, {2, 2}, {4, 4}, {6, 6}}

        -- Proxy helpers.
        local txn_proxy = require('test.box.lua.txn_proxy')
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

        -- Check if count on the primary kay with given key and iterator gives
        -- the expected result for the given transaction.
        local function check(tx, it, key, expected_count)
            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline

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

        -- The test routine.
        local function test(tx)
            -- Full key.
            check(tx, 'lt', {3, 3}, 4)
            check(tx, 'le', {3, 3}, 5)
            for _, key in pairs(existing_keys) do
                check(tx, 'eq', key, 1)
                check(tx, 'req', key, 1)
            end
            check(tx, 'ge', {3, 3}, 5)
            check(tx, 'gt', {3, 3}, 4)

            -- Partial key.
            check(tx, 'lt', {3}, 3)
            check(tx, 'le', {3}, 6)
            for _, key in pairs({{1}, {3}, {5}}) do
                check(tx, 'eq', key, 3)
                check(tx, 'req', key, 3)
            end
            check(tx, 'ge', {3}, 6)
            check(tx, 'gt', {3}, 3)

            -- Empty key.
            check(tx, 'lt', {}, 9)
            check(tx, 'le', {}, 9)
            check(tx, 'eq', {}, 9)
            check(tx, 'req', {}, 9)
            check(tx, 'ge', {}, 9)
            check(tx, 'gt', {}, 9)

            -- Full unexisting key.
            check(tx, 'lt', {1, 1}, 0)
            check(tx, 'lt', {0, 0}, 0)
            check(tx, 'le', {1, 0}, 0)
            check(tx, 'eq', {3, 4}, 0)
            check(tx, 'req', {3, 4}, 0)
            check(tx, 'ge', {5, 6}, 0)
            check(tx, 'ge', {8, 8}, 0)
            check(tx, 'gt', {5, 5}, 0)
            check(tx, 'gt', {8, 8}, 0)

            -- Partial unexisting key.
            check(tx, 'lt', {1}, 0)
            check(tx, 'lt', {0}, 0)
            check(tx, 'le', {0}, 0)
            check(tx, 'eq', {2}, 0)
            check(tx, 'req', {2}, 0)
            check(tx, 'ge', {6}, 0)
            check(tx, 'ge', {8}, 0)
            check(tx, 'gt', {5}, 0)
            check(tx, 'gt', {8}, 0)
        end

        -- Check for consistency of counts in a transaction: first performs a
        -- number of counts, and then performs various inserts, replaces and
        -- deletes of keys and checks if the count results remain the same.
        local function check_consistency()
            -- The location of the callee.
            local existing_keys = s:select()
            local old_len = #existing_keys
            local tx = txn_proxy.new()

            tx:begin()
            test(tx)
            for _, new_key in pairs(unexisting_keys) do
                s:insert(new_key)
                test(tx)
            end
            for _, old_key in pairs(existing_keys) do
                s:delete(old_key)
                test(tx)
            end
            for _, old_key in pairs(unexisting_keys) do
                s:delete(old_key)
                test(tx)
            end
            for _, new_key in pairs(existing_keys) do
                s:insert(new_key)
                test(tx)
            end
            t.assert_equals(tx:commit(), success)
            t.assert_equals(s:len(), old_len)
            t.assert_equals(s:select(), existing_keys)
        end

        -- Check if the local tables are consistent with space contents.
        local function check_space()
            t.assert_equals(s:len(), #existing_keys)
            t.assert_equals(s:select(), existing_keys)
        end

        -- Insert the keys to exist.
        for _, key in pairs(existing_keys) do
            s:insert(key)
        end
        check_space()

        -- Consistency in the read view.
        check_consistency()
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

        -- The test space data.
        local existing_keys = {{1, 1}, {1, 3}, {1, 5},
                               {3, 1}, {3, 3}, {3, 5},
                               {5, 1}, {5, 3}, {5, 5}}

        -- Prepare proxies.
        local txn_proxy = require('test.box.lua.txn_proxy')
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
        local function test(it, key, offset, expect, op, tuple, tx1_result)
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

        -- Check if the local tables are consistent with space contents.
        local function check_space()
            t.assert_equals(s:len(), #existing_keys)
            t.assert_equals(s:select(), existing_keys)
        end

        -- Insert the keys to exist.
        for _, key in pairs(existing_keys) do
            s:insert(key)
        end
        check_space()

        -- Conflict (select & replace/delete first).
        for _, op in pairs({'replace', 'delete'}) do
            -- Full key.
            test('lt', {3, 3}, 2, {{1, 3}, {1, 1}}, op, {1, 3}, conflict)
            test('lt', {3, 2}, 2, {{1, 3}, {1, 1}}, op, {1, 3}, conflict)
            test('le', {3, 2}, 2, {{1, 3}, {1, 1}}, op, {1, 3}, conflict)
            test('le', {3, 1}, 2, {{1, 3}, {1, 1}}, op, {1, 3}, conflict)
            test('ge', {3, 5}, 2, {{5, 3}, {5, 5}}, op, {5, 3}, conflict)
            test('ge', {3, 4}, 2, {{5, 3}, {5, 5}}, op, {5, 3}, conflict)
            test('gt', {3, 3}, 2, {{5, 3}, {5, 5}}, op, {5, 3}, conflict)
            test('gt', {3, 4}, 2, {{5, 3}, {5, 5}}, op, {5, 3}, conflict)

            -- Partial key.
            test('lt', {3}, 1, {{1, 3}, {1, 1}}, op, {1, 3}, conflict)
            test('lt', {4}, 4, {{1, 3}, {1, 1}}, op, {1, 3}, conflict)
            test('le', {3}, 4, {{1, 3}, {1, 1}}, op, {1, 3}, conflict)
            test('le', {4}, 4, {{1, 3}, {1, 1}}, op, {1, 3}, conflict)
            test('eq', {3}, 2, {{3, 5}}, op, {3, 5}, conflict)
            test('req', {3}, 2, {{3, 1}}, op, {3, 1}, conflict)
            test('ge', {5}, 1, {{5, 3}, {5, 5}}, op, {5, 3}, conflict)
            test('ge', {4}, 1, {{5, 3}, {5, 5}}, op, {5, 3}, conflict)
            test('gt', {3}, 1, {{5, 3}, {5, 5}}, op, {5, 3}, conflict)
            test('gt', {4}, 1, {{5, 3}, {5, 5}}, op, {5, 3}, conflict)
        end
        check_space()

        -- No conflict (select & replace skipped).
        -- Full key, reverse iterators.
        for _, tuple in pairs({{3, 1}, {1, 5}}) do
            test('lt', {3, 3}, 2, {{1, 3}, {1, 1}}, 'replace', tuple, success)
            test('lt', {3, 2}, 2, {{1, 3}, {1, 1}}, 'replace', tuple, success)
            test('le', {3, 2}, 2, {{1, 3}, {1, 1}}, 'replace', tuple, success)
            test('le', {3, 1}, 2, {{1, 3}, {1, 1}}, 'replace', tuple, success)
        end
        -- Full key, forward iterators.
        for _, tuple in pairs({{3, 5}, {5, 1}}) do
            test('ge', {3, 5}, 2, {{5, 3}, {5, 5}}, 'replace', tuple, success)
            test('ge', {3, 4}, 2, {{5, 3}, {5, 5}}, 'replace', tuple, success)
            test('gt', {3, 3}, 2, {{5, 3}, {5, 5}}, 'replace', tuple, success)
            test('gt', {3, 4}, 2, {{5, 3}, {5, 5}}, 'replace', tuple, success)
        end
        -- Partial key, reverse iterators.
        for _, tuple in pairs({{3, 5}, {3, 3}, {3, 1}, {1, 5}}) do
            test('lt', {5}, 4, {{1, 3}, {1, 1}}, 'replace', tuple, success)
            test('lt', {4}, 4, {{1, 3}, {1, 1}}, 'replace', tuple, success)
            test('le', {3}, 4, {{1, 3}, {1, 1}}, 'replace', tuple, success)
            test('le', {4}, 4, {{1, 3}, {1, 1}}, 'replace', tuple, success)
        end
        -- Partial key, reverse iterators.
        for _, tuple in pairs({{3, 1}, {3, 3}, {3, 5}, {5, 1}}) do
            test('ge', {3}, 4, {{5, 3}, {5, 5}}, 'replace', tuple, success)
            test('ge', {2}, 4, {{5, 3}, {5, 5}}, 'replace', tuple, success)
            test('gt', {1}, 4, {{5, 3}, {5, 5}}, 'replace', tuple, success)
            test('gt', {2}, 4, {{5, 3}, {5, 5}}, 'replace', tuple, success)
        end
        -- Partial key, equality iterators.
        test('eq', {3}, 2, {{3, 5}}, 'replace', {3, 1}, success)
        test('eq', {3}, 2, {{3, 5}}, 'replace', {3, 3}, success)
        test('req', {3}, 2, {{3, 1}}, 'replace', {3, 5}, success)
        test('req', {3}, 2, {{3, 1}}, 'replace', {3, 3}, success)
        check_space()

        -- Conflict (select & delete skipped).
        -- Full key, reverse iterators.
        for _, tuple in pairs({{3, 1}, {1, 5}}) do
            test('lt', {3, 3}, 2, {{1, 3}, {1, 1}}, 'delete', tuple, conflict)
            test('lt', {3, 2}, 2, {{1, 3}, {1, 1}}, 'delete', tuple, conflict)
            test('le', {3, 2}, 2, {{1, 3}, {1, 1}}, 'delete', tuple, conflict)
            test('le', {3, 1}, 2, {{1, 3}, {1, 1}}, 'delete', tuple, conflict)
        end
        -- Full key, forward iterators.
        for _, tuple in pairs({{3, 5}, {5, 1}}) do
            test('ge', {3, 5}, 2, {{5, 3}, {5, 5}}, 'delete', tuple, conflict)
            test('ge', {3, 4}, 2, {{5, 3}, {5, 5}}, 'delete', tuple, conflict)
            test('gt', {3, 3}, 2, {{5, 3}, {5, 5}}, 'delete', tuple, conflict)
            test('gt', {3, 4}, 2, {{5, 3}, {5, 5}}, 'delete', tuple, conflict)
        end
        -- Partial key, reverse iterators.
        for _, tuple in pairs({{3, 5}, {3, 3}, {3, 1}, {1, 5}}) do
            test('lt', {5}, 4, {{1, 3}, {1, 1}}, 'delete', tuple, conflict)
            test('lt', {4}, 4, {{1, 3}, {1, 1}}, 'delete', tuple, conflict)
            test('le', {3}, 4, {{1, 3}, {1, 1}}, 'delete', tuple, conflict)
            test('le', {4}, 4, {{1, 3}, {1, 1}}, 'delete', tuple, conflict)
        end
        -- Partial key, reverse iterators.
        for _, tuple in pairs({{3, 1}, {3, 3}, {3, 5}, {5, 1}}) do
            test('ge', {3}, 4, {{5, 3}, {5, 5}}, 'delete', tuple, conflict)
            test('ge', {2}, 4, {{5, 3}, {5, 5}}, 'delete', tuple, conflict)
            test('gt', {1}, 4, {{5, 3}, {5, 5}}, 'delete', tuple, conflict)
            test('gt', {2}, 4, {{5, 3}, {5, 5}}, 'delete', tuple, conflict)
        end
        -- Partial key, equality iterators.
        test('eq', {3}, 2, {{3, 5}}, 'delete', {3, 1}, conflict)
        test('eq', {3}, 2, {{3, 5}}, 'delete', {3, 3}, conflict)
        test('req', {3}, 2, {{3, 1}}, 'delete', {3, 5}, conflict)
        test('req', {3}, 2, {{3, 1}}, 'delete', {3, 3}, conflict)
        check_space()

        -- Conflict (select & insert potentially skipped).
        -- Full key, reverse iterators.
        for _, tuple in pairs({{3, 0}, {1, 6}, {1, 4}}) do
            test('lt', {3, 3}, 2, {{1, 3}, {1, 1}}, 'insert', tuple, conflict)
            test('lt', {3, 2}, 2, {{1, 3}, {1, 1}}, 'insert', tuple, conflict)
            test('le', {3, 2}, 2, {{1, 3}, {1, 1}}, 'insert', tuple, conflict)
            test('le', {3, 1}, 2, {{1, 3}, {1, 1}}, 'insert', tuple, conflict)
        end
        -- Full key, forward iterators.
        for _, tuple in pairs({{3, 6}, {5, 0}, {5, 2}}) do
            test('ge', {3, 5}, 2, {{5, 3}, {5, 5}}, 'insert', tuple, conflict)
            test('ge', {3, 4}, 2, {{5, 3}, {5, 5}}, 'insert', tuple, conflict)
            test('gt', {3, 3}, 2, {{5, 3}, {5, 5}}, 'insert', tuple, conflict)
            test('gt', {3, 4}, 2, {{5, 3}, {5, 5}}, 'insert', tuple, conflict)
        end
        -- Partial key, reverse iterators.
        for _, tuple in pairs({{3, 6}, {3, 4}, {3, 2}, {1, 6}, {1, 4}}) do
            test('lt', {5}, 4, {{1, 3}, {1, 1}}, 'insert', tuple, conflict)
            test('lt', {4}, 4, {{1, 3}, {1, 1}}, 'insert', tuple, conflict)
            test('le', {3}, 4, {{1, 3}, {1, 1}}, 'insert', tuple, conflict)
            test('le', {4}, 4, {{1, 3}, {1, 1}}, 'insert', tuple, conflict)
        end
        -- Partial key, reverse iterators.
        for _, tuple in pairs({{3, 0}, {3, 2}, {3, 4}, {5, 0}, {5, 2}}) do
            test('ge', {3}, 4, {{5, 3}, {5, 5}}, 'insert', tuple, conflict)
            test('ge', {2}, 4, {{5, 3}, {5, 5}}, 'insert', tuple, conflict)
            test('gt', {1}, 4, {{5, 3}, {5, 5}}, 'insert', tuple, conflict)
            test('gt', {2}, 4, {{5, 3}, {5, 5}}, 'insert', tuple, conflict)
        end
        -- Partial key, equality iterators.
        for _, tuple in pairs({{3, 0}, {3, 2}, {3, 4}, {3, 6}}) do
            test('eq', {3}, 2, {{3, 5}}, 'insert', tuple, conflict)
            test('req', {3}, 2, {{3, 1}}, 'insert', tuple, conflict)
        end
        check_space()

        -- No conflict (select & replace/delete not matching).
        for _, op in pairs({'replace', 'delete'}) do
            -- Full key, reverse iterators.
            for _, tuple in pairs({{3, 3}, {3, 5}, {5, 1}, {5, 3}, {5, 5}}) do
                test('lt', {3, 3}, 2, {{1, 3}, {1, 1}}, op, tuple, success)
                test('lt', {3, 2}, 2, {{1, 3}, {1, 1}}, op, tuple, success)
                test('le', {3, 2}, 2, {{1, 3}, {1, 1}}, op, tuple, success)
                test('le', {3, 1}, 2, {{1, 3}, {1, 1}}, op, tuple, success)
            end
            -- Full key, forward iterators.
            for _, tuple in pairs({{1, 1}, {1, 3}, {1, 5}, {3, 1}, {3, 3}}) do
                test('ge', {3, 5}, 2, {{5, 3}, {5, 5}}, op, tuple, success)
                test('ge', {3, 4}, 2, {{5, 3}, {5, 5}}, op, tuple, success)
                test('gt', {3, 3}, 2, {{5, 3}, {5, 5}}, op, tuple, success)
                test('gt', {3, 4}, 2, {{5, 3}, {5, 5}}, op, tuple, success)
            end
            -- Partial key, reverse iterators.
            for _, tuple in pairs({{5, 1}, {5, 3}, {5, 5}}) do
                test('lt', {5}, 4, {{1, 3}, {1, 1}}, op, tuple, success)
                test('lt', {4}, 4, {{1, 3}, {1, 1}}, op, tuple, success)
                test('le', {3}, 4, {{1, 3}, {1, 1}}, op, tuple, success)
                test('le', {4}, 4, {{1, 3}, {1, 1}}, op, tuple, success)
            end
            -- Partial key, reverse iterators.
            for _, tuple in pairs({{1, 1}, {1, 3}, {1, 5}}) do
                test('ge', {3}, 4, {{5, 3}, {5, 5}}, op, tuple, success)
                test('ge', {2}, 4, {{5, 3}, {5, 5}}, op, tuple, success)
                test('gt', {1}, 4, {{5, 3}, {5, 5}}, op, tuple, success)
                test('gt', {2}, 4, {{5, 3}, {5, 5}}, op, tuple, success)
            end
            -- Partial key, equality iterators.
            for _, tuple in pairs({{1, 1}, {1, 3}, {1, 5}, {5, 1}, {5, 5}}) do
                test('eq', {3}, 2, {{3, 5}}, op, tuple, success)
                test('req', {3}, 2, {{3, 1}}, op, tuple, success)
            end
        end
        check_space()

        -- No conflict (select & insert not matching).
        -- Full key, reverse iterators.
        for _, tuple in pairs({{3, 4}, {3, 6}, {4, 4}, {5, 0}, {5, 6}}) do
            test('lt', {3, 3}, 2, {{1, 3}, {1, 1}}, 'insert', tuple, success)
            test('lt', {3, 2}, 2, {{1, 3}, {1, 1}}, 'insert', tuple, success)
            test('le', {3, 2}, 2, {{1, 3}, {1, 1}}, 'insert', tuple, success)
            test('le', {3, 1}, 2, {{1, 3}, {1, 1}}, 'insert', tuple, success)
        end
        -- Full key, forward iterators.
        for _, tuple in pairs({{1, 0}, {1, 6}, {2, 2}, {3, 0}}) do
            test('ge', {3, 5}, 2, {{5, 3}, {5, 5}}, 'insert', tuple, success)
            test('ge', {3, 4}, 2, {{5, 3}, {5, 5}}, 'insert', tuple, success)
            test('gt', {3, 3}, 2, {{5, 3}, {5, 5}}, 'insert', tuple, success)
            test('gt', {3, 4}, 2, {{5, 3}, {5, 5}}, 'insert', tuple, success)
        end
        -- Partial key, reverse iterators.
        for _, tuple in pairs({{5, 0}, {5, 2}, {5, 4}, {5, 6}, {6, 6}}) do
            test('lt', {5}, 4, {{1, 3}, {1, 1}}, 'insert', tuple, success)
            test('lt', {4}, 4, {{1, 3}, {1, 1}}, 'insert', tuple, success)
            test('le', {3}, 4, {{1, 3}, {1, 1}}, 'insert', tuple, success)
            test('le', {4}, 4, {{1, 3}, {1, 1}}, 'insert', tuple, success)
        end
        -- Partial key, reverse iterators.
        for _, tuple in pairs({{1, 0}, {1, 2}, {1, 4}, {1, 6}}) do
            test('ge', {3}, 4, {{5, 3}, {5, 5}}, 'insert', tuple, success)
            test('ge', {2}, 4, {{5, 3}, {5, 5}}, 'insert', tuple, success)
            test('gt', {1}, 4, {{5, 3}, {5, 5}}, 'insert', tuple, success)
            test('gt', {2}, 4, {{5, 3}, {5, 5}}, 'insert', tuple, success)
        end
        -- Partial key, equality iterators.
        for _, tuple in pairs({{1, 0}, {1, 6}, {4, 4}, {5, 0}, {5, 6}}) do
            test('eq', {3}, 2, {{3, 5}}, 'insert', tuple, success)
            test('req', {3}, 2, {{3, 1}}, 'insert', tuple, success)
        end
        check_space()
    end)
end

g_mvcc.test_offset_consistency = function()
    g_mvcc.server:exec(function()
        -- The test space.
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'unsigned'}, {2, 'unsigned'}}})

        -- Create a space to make tested transactions writing - only writing
        -- transactions can cause conflicts with aborts.
        box.schema.space.create('make_conflicting_writer')
        box.space.make_conflicting_writer:create_index('pk', {sequence = true})

        local existing_keys = {{1, 1}, {1, 3}, {1, 5},
                               {3, 1}, {3, 3}, {3, 5},
                               {5, 1}, {5, 3}, {5, 5}}
        local unexisting_keys = {{1, 0}, {1, 2}, {1, 4}, {1, 6},
                                 {3, 0}, {3, 2}, {3, 4}, {3, 6},
                                 {5, 0}, {5, 2}, {5, 4}, {5, 6},
                                 {0, 0}, {2, 2}, {4, 4}, {6, 6}}

        -- The transaction proxy.
        local txn_proxy = require('test.box.lua.txn_proxy')
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

        -- Check if select in the test space with given key and iterator gives
        -- the expected result.
        local function check(tx, it, key, offset, expect)
            -- The location of the callee.
            local file = debug.getinfo(2, 'S').source
            local line = debug.getinfo(2, 'l').currentline

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

        -- The test routine.
        local function test(tx)
            -- Full key.
            check(tx, 'lt', {3, 3}, 2, {{1, 3}, {1, 1}})
            check(tx, 'lt', {3, 2}, 2, {{1, 3}, {1, 1}})
            check(tx, 'le', {3, 2}, 2, {{1, 3}, {1, 1}})
            check(tx, 'le', {3, 1}, 2, {{1, 3}, {1, 1}})
            check(tx, 'ge', {3, 5}, 2, {{5, 3}, {5, 5}})
            check(tx, 'ge', {3, 4}, 2, {{5, 3}, {5, 5}})
            check(tx, 'gt', {3, 3}, 2, {{5, 3}, {5, 5}})
            check(tx, 'gt', {3, 4}, 2, {{5, 3}, {5, 5}})

            -- Partial key.
            check(tx, 'lt', {3}, 1, {{1, 3}, {1, 1}})
            check(tx, 'lt', {4}, 4, {{1, 3}, {1, 1}})
            check(tx, 'lt', {5}, 4, {{1, 3}, {1, 1}})
            check(tx, 'le', {3}, 4, {{1, 3}, {1, 1}})
            check(tx, 'le', {4}, 4, {{1, 3}, {1, 1}})
            check(tx, 'eq', {3}, 2, {{3, 5}})
            check(tx, 'req', {3}, 2, {{3, 1}})
            check(tx, 'ge', {5}, 1, {{5, 3}, {5, 5}})
            check(tx, 'ge', {4}, 1, {{5, 3}, {5, 5}})
            check(tx, 'ge', {3}, 4, {{5, 3}, {5, 5}})
            check(tx, 'ge', {2}, 4, {{5, 3}, {5, 5}})
            check(tx, 'gt', {1}, 4, {{5, 3}, {5, 5}})
            check(tx, 'gt', {2}, 4, {{5, 3}, {5, 5}})
            check(tx, 'gt', {3}, 1, {{5, 3}, {5, 5}})
            check(tx, 'gt', {4}, 1, {{5, 3}, {5, 5}})

            -- Skip everything.
            for _, offset in pairs({3, 4, 5, 50}) do
                check(tx, 'lt', {3}, offset, {})
                check(tx, 'lt', {2}, offset, {})
                check(tx, 'le', {2}, offset, {})
                check(tx, 'le', {1}, offset, {})
                for _, key in pairs({{1}, {3}, {5}}) do
                    check(tx, 'eq', key, offset, {})
                    check(tx, 'req', key, offset, {})
                end
                check(tx, 'ge', {5}, offset, {})
                check(tx, 'ge', {4}, offset, {})
                check(tx, 'gt', {4}, offset, {})
                check(tx, 'gt', {3}, offset, {})
            end
        end

        -- Check for consistency of select in a transaction. First performs a
        -- number of selects, and then performs various inserts, replaces and
        -- deletes of keys and checks if select results remain the same.
        local function check_consistency()
            -- The location of the callee.
            local existing_keys = s:select()
            local old_len = #existing_keys
            local tx = txn_proxy.new()

            tx:begin()
            test(tx)
            for _, new_key in pairs(unexisting_keys) do
                s:insert(new_key)
                test(tx)
            end
            for _, old_key in pairs(existing_keys) do
                s:delete(old_key)
                test(tx)
            end
            for _, old_key in pairs(unexisting_keys) do
                s:delete(old_key)
                test(tx)
            end
            for _, new_key in pairs(existing_keys) do
                s:insert(new_key)
                test(tx)
            end
            t.assert_equals(tx:commit(), success)
            t.assert_equals(s:len(), old_len)
            t.assert_equals(s:select(), existing_keys)
        end

        -- Check if the local tables are consistent with space contents.
        local function check_space()
            t.assert_equals(s:len(), #existing_keys)
            t.assert_equals(s:select(), existing_keys)
        end

        -- Insert the keys to exist.
        for _, key in pairs(existing_keys) do
            s:insert(key)
        end
        check_space()

        -- Consistency in the read view.
        check_consistency()
        check_space()
    end)
end
