local server = require('luatest.server')
local t = require('luatest')

local g = t.group('update')

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create('tweedledum')
        s:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.tweedledum then
            box.space.tweedledum:truncate()
        end
    end)
end)

-- Test '#' on the primary key field and with a non-integer count.
g.test_delete_field = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum
        s:insert{1000001, 1000002, 1000003, 1000004, 1000005}

        t.assert_error_msg_equals(
            "Attempt to modify a tuple field which is part of" ..
            " primary index in space 'tweedledum'",
            s.update, s, {1000001}, {{'#', 1, 1}}
        )
        t.assert_error_msg_equals(
            "Argument type in operation '#' on field 1 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {1000001}, {{'#', 1, "only one record please"}}
        )
    end)
end

-- Test arithmetic operations: +, -, |, &, ^.
g.test_arithmetic = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum
        s:insert{1, 0}

        t.assert_equals(s:update(1, {{'+', 2, 10}}):totable(), {1, 10})
        t.assert_equals(s:update(1, {{'+', 2, 15}}):totable(), {1, 25})
        t.assert_equals(s:update(1, {{'-', 2, 5}}):totable(), {1, 20})
        t.assert_equals(s:update(1, {{'-', 2, 20}}):totable(), {1, 0})
        t.assert_equals(s:update(1, {{'|', 2, 0x9}}):totable(), {1, 9})
        t.assert_equals(s:update(1, {{'|', 2, 0x6}}):totable(), {1, 15})
        t.assert_equals(s:update(1, {{'&', 2, 0xabcde}}):totable(), {1, 14})
        t.assert_equals(s:update(1, {{'&', 2, 0x2}}):totable(), {1, 2})
        t.assert_equals(s:update(1, {{'^', 2, 0xa2}}):totable(), {1, 160})
        t.assert_equals(s:update(1, {{'^', 2, 0xa2}}):totable(), {1, 2})
    end)
end

-- Test deleting multiple fields with '#'.
g.test_delete_multiple_fields = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum
        s:insert{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}

        t.assert_equals(
            s:update({0}, {{'#', 42, 1}}):totable(),
            {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}
        )
        t.assert_error_msg_equals(
            "Argument type in operation '#' on field 4 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'#', 4, 'abirvalg'}}
        )
        t.assert_equals(
            s:update({0}, {{'#', 2, 1}, {'#', 4, 2}, {'#', 6, 1}}):totable(),
            {0, 2, 3, 6, 7, 9, 10, 11, 12, 13, 14, 15}
        )
        t.assert_equals(
            s:update({0}, {{'#', 4, 3}}):totable(),
            {0, 2, 3, 10, 11, 12, 13, 14, 15}
        )
        t.assert_equals(
            s:update({0}, {{'#', 5, 123456}}):totable(),
            {0, 2, 3, 10}
        )
        t.assert_equals(
            s:update({0}, {{'#', 3, 4294967295}}):totable(),
            {0, 2}
        )
        t.assert_error_msg_equals(
            "Field 2 UPDATE error: cannot delete 0 fields",
            s.update, s, {0}, {{'#', 2, 0}}
        )
    end)
end

-- Test insert field '!' and combined insert/delete/set ops.
g.test_insert_field = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum

        s:insert{1, 3, 6, 9}
        t.assert_equals(
            s:update({1}, {{'!', 2, 2}}):totable(),
            {1, 2, 3, 6, 9}
        )
        t.assert_equals(
            s:update({1}, {
                {'!', 4, 4}, {'!', 4, 5}, {'!', 5, 7}, {'!', 5, 8},
            }):totable(),
            {1, 2, 3, 5, 8, 7, 4, 6, 9}
        )
        t.assert_equals(
            s:update({1}, {
                {'!', 10, 10}, {'!', 10, 11}, {'!', 10, 12},
            }):totable(),
            {1, 2, 3, 5, 8, 7, 4, 6, 9, 12, 11, 10}
        )
        s:truncate()

        s:insert{1, 'tuple'}
        t.assert_equals(
            s:update({1}, {
                {'#', 2, 1}, {'!', 2, 'inserted tuple'}, {'=', 3, 'set tuple'},
            }):totable(),
            {1, 'inserted tuple', 'set tuple'}
        )
        s:truncate()

        s:insert{1, 'tuple'}
        t.assert_equals(
            s:update({1}, {
                {'=', 2, 'set tuple'}, {'!', 2, 'inserted tuple'}, {'#', 3, 1},
            }):totable(),
            {1, 'inserted tuple'}
        )
        t.assert_error_msg_equals(
            "Attempt to modify a tuple field which is part of" ..
            " primary index in space 'tweedledum'",
            s.update, s, {1}, {{'!', 1, 3}, {'!', 1, 2}}
        )
    end)
end

-- Test assign '=' operator.
g.test_assign = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum
        s:replace{1, 'field string value'}
        t.assert_equals(
            s:update({1}, {
                {'=', 2, 'new field string value'},
                {'=', 3, 42},
                {'=', 4, 0xdeadbeef},
            }):totable(),
            {1, 'new field string value', 42, 3735928559}
        )
    end)
end

-- Test multiple arithmetic ops targeting different fields in one update.
g.test_multi_ops_in_update = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum
        s:replace{1, 'str', 42, 3735928559}
        t.assert_equals(
            s:update({1}, {
                {'+', 3, 16},
                {'&', 4, 0xffff0000},
                {'|', 4, 0x0000a0a0},
                {'^', 4, 0xffff00aa},
            }):totable(),
            {1, 'str', 58, 559063050}
        )
    end)
end

-- Test splice ':' operation.
g.test_splice = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum

        s:replace{1953719668, 'something to splice'}
        t.assert_equals(
            s:update(1953719668, {{':', 2, 1, 4, 'no'}}):totable(),
            {1953719668, 'nothing to splice'}
        )
        t.assert_equals(
            s:update(1953719668, {{':', 2, 1, 2, 'every'}}):totable(),
            {1953719668, 'everything to splice'}
        )
        t.assert_equals(
            s:update(1953719668, {{':', 2, 100, 2, 'every'}}):totable(),
            {1953719668, 'everything to spliceevery'}
        )
        t.assert_error_msg_equals(
            "SPLICE error on field 2: offset is out of bound",
            s.update, s, 1953719668, {{':', 2, -100, 2, 'every'}}
        )
        s:truncate()

        s:insert{1953719668, 'hello world'}
        t.assert_equals(
            s:update(1953719668, {{'=', 2, 'bye, world'}}):totable(),
            {1953719668, 'bye, world'}
        )
        s:delete{1953719668}

        s:replace({10, 'abcde'})
        t.assert_error_msg_equals(
            "SPLICE error on field 2: offset is out of bound",
            s.update, s, 10, {{':', 2, 0, 0, '!'}}
        )
        t.assert_equals(
            s:update(10, {{':', 2, 1, 0, '('}}):totable(),
            {10, '(abcde'}
        )
        t.assert_equals(
            s:update(10, {{':', 2, 2, 0, '({'}}):totable(),
            {10, '(({abcde'}
        )
        t.assert_equals(
            s:update(10, {{':', 2, -1, 0, ')'}}):totable(),
            {10, '(({abcde)'}
        )
        t.assert_equals(
            s:update(10, {{':', 2, -2, 0, '})'}}):totable(),
            {10, '(({abcde}))'}
        )
    end)
end

-- Test that update with field index 0 fails.
g.test_zero_field = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum
        s:insert{48}
        t.assert_error_msg_equals(
            "Field 0 was not found in the tuple",
            s.update, s, 48, {{'=', 0, 'hello'}}
        )
    end)
end

-- Test push/pop semantics with the special index -1.
g.test_push_pop = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum

        s:insert{1684234849}
        t.assert_equals(
            s:update({1684234849}, {{'#', 2, 1}}):totable(),
            {1684234849}
        )
        t.assert_equals(
            s:update({1684234849}, {{'!', -1, 'push1'}}):totable(),
            {1684234849, 'push1'}
        )
        t.assert_equals(
            s:update({1684234849}, {{'!', -1, 'push2'}}):totable(),
            {1684234849, 'push1', 'push2'}
        )
        t.assert_equals(
            s:update({1684234849}, {{'!', -1, 'push3'}}):totable(),
            {1684234849, 'push1', 'push2', 'push3'}
        )
        t.assert_equals(
            s:update({1684234849},
                     {{'#', 2, 1}, {'!', -1, 'swap1'}}):totable(),
            {1684234849, 'push2', 'push3', 'swap1'}
        )
        t.assert_equals(
            s:update({1684234849},
                     {{'#', 2, 1}, {'!', -1, 'swap2'}}):totable(),
            {1684234849, 'push3', 'swap1', 'swap2'}
        )
        t.assert_equals(
            s:update({1684234849},
                     {{'#', 2, 1}, {'!', -1, 'swap3'}}):totable(),
            {1684234849, 'swap1', 'swap2', 'swap3'}
        )
        t.assert_equals(
            s:update({1684234849},
                     {{'#', -1, 1}, {'!', -1, 'noop1'}}):totable(),
            {1684234849, 'swap1', 'swap2', 'noop1'}
        )
        t.assert_equals(
            s:update({1684234849},
                     {{'#', -1, 1}, {'!', -1, 'noop2'}}):totable(),
            {1684234849, 'swap1', 'swap2', 'noop2'}
        )
        t.assert_equals(
            s:update({1684234849},
                     {{'#', -1, 1}, {'!', -1, 'noop3'}}):totable(),
            {1684234849, 'swap1', 'swap2', 'noop3'}
        )
    end)
end

-- Test negative field indexes for all update operations on tuples.
g.test_negative_indexes = function(cg)
    cg.server:exec(function()
        local base = {1, 2, 3, 4, 5}

        -- '!' insert with negative indexes
        t.assert_error_msg_equals(
            "Field 0 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'!', 0, 'Test'}}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'!', -1, 'Test'}}):totable(),
            {1, 2, 3, 4, 5, 'Test'}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'!', -3, 'Test'}}):totable(),
            {1, 2, 3, 'Test', 4, 5}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'!', -5, 'Test'}}):totable(),
            {1, 'Test', 2, 3, 4, 5}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'!', -6, 'Test'}}):totable(),
            {'Test', 1, 2, 3, 4, 5}
        )
        t.assert_error_msg_equals(
            "Field -7 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'!', -7, 'Test'}}
        )
        t.assert_error_msg_equals(
            "Field -100500 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'!', -100500, 'Test'}}
        )

        -- '=' set with negative indexes
        t.assert_error_msg_equals(
            "Field 0 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'=', 0, 'Test'}}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'=', -1, 'Test'}}):totable(),
            {1, 2, 3, 4, 'Test'}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'=', -3, 'Test'}}):totable(),
            {1, 2, 'Test', 4, 5}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'=', -5, 'Test'}}):totable(),
            {'Test', 2, 3, 4, 5}
        )
        t.assert_error_msg_equals(
            "Field -6 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'=', -6, 'Test'}}
        )
        t.assert_error_msg_equals(
            "Field -100500 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'=', -100500, 'Test'}}
        )

        -- '+' arithmetic with negative indexes
        t.assert_error_msg_equals(
            "Field 0 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'+', 0, 100}}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'+', -1, 100}}):totable(),
            {1, 2, 3, 4, 105}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'+', -3, 100}}):totable(),
            {1, 2, 103, 4, 5}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'+', -5, 100}}):totable(),
            {101, 2, 3, 4, 5}
        )
        t.assert_error_msg_equals(
            "Field -6 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'+', -6, 100}}
        )
        t.assert_error_msg_equals(
            "Field -100500 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'+', -100500, 100}}
        )

        -- '|' bitwise-or with negative indexes
        t.assert_error_msg_equals(
            "Field 0 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'|', 0, 100}}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'|', -1, 100}}):totable(),
            {1, 2, 3, 4, 101}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'|', -3, 100}}):totable(),
            {1, 2, 103, 4, 5}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'|', -5, 100}}):totable(),
            {101, 2, 3, 4, 5}
        )
        t.assert_error_msg_equals(
            "Field -6 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'|', -6, 100}}
        )
        t.assert_error_msg_equals(
            "Field -100500 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'|', -100500, 100}}
        )

        -- '#' delete with negative indexes
        t.assert_error_msg_equals(
            "Field 0 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'#', 0, 1}}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'#', -1, 1}}):totable(),
            {1, 2, 3, 4}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'#', -3, 1}}):totable(),
            {1, 2, 4, 5}
        )
        t.assert_equals(
            box.tuple.new(base):update({{'#', -5, 1}}):totable(),
            {2, 3, 4, 5}
        )
        t.assert_error_msg_equals(
            "Field -6 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'#', -6, 1}}
        )
        t.assert_error_msg_equals(
            "Field -100500 was not found in the tuple",
            box.tuple.new(base).update, box.tuple.new(base),
            {{'#', -100500, 1}}
        )
    end)
end

-- gh-416: UPDATEs must survive a server restart (WAL replay test).
g.test_gh_416_restart = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum
        s:replace({10, 'abcde'})
        s:update(10, {{':', 2, 1, 0, '('}})
        s:update(10, {{':', 2, 2, 0, '({'}})
        s:update(10, {{':', 2, -1, 0, ')'}})
        s:update(10, {{':', 2, -2, 0, '})'}})
        s:replace({48})
        s:replace({1684234849, 'push1', 'push2', 'push3'})
        s:update({1684234849}, {{'#', 2, 1}, {'!', -1, 'swap1'}})
        s:update({1684234849}, {{'#', 2, 1}, {'!', -1, 'swap2'}})
        s:update({1684234849}, {{'#', 2, 1}, {'!', -1, 'swap3'}})
        s:update({1684234849}, {{'#', -1, 1}, {'!', -1, 'noop1'}})
        s:update({1684234849}, {{'#', -1, 1}, {'!', -1, 'noop2'}})
        s:update({1684234849}, {{'#', -1, 1}, {'!', -1, 'noop3'}})
    end)
    cg.server:restart()
    cg.server:exec(function()
        local s = box.space.tweedledum
        t.assert_equals(s:get{10}:totable(), {10, '(({abcde}))'})
        t.assert_equals(s:get{48}:totable(), {48})
        t.assert_equals(
            s:get{1684234849}:totable(),
            {1684234849, 'swap1', 'swap2', 'noop3'}
        )
    end)
end

-- gh-521: Cryptic error messages for malformed update calls.
g.test_gh_521_error_messages = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum
        s:insert{1, 2, 3}

        t.assert_error_msg_equals(
            "Usage: index:update(key, ops)",
            s.update, s, {1}
        )
        t.assert_error_msg_equals(
            "update operation must be an array {op,..}",
            s.update, s, {1}, {'=', 1, 1}
        )
    end)
end

-- gh-528: Different numeric types in arithmetic, overflow checks.
g.test_gh_528_arithmetic_types = function(cg)
    cg.server:exec(function()
        local ffi = require('ffi')
        local decimal = require('decimal')
        local s = box.space.tweedledum
        s:insert{0, -1}

        -- '+' with various types
        t.assert_error_msg_equals(
            "Argument type in operation '+' on field 2 does not" ..
            " match field type: expected a number",
            s.update, s, {0}, {{'+', 2, "a"}}
        )
        t.assert_equals(s:update({0}, {{'+', 2, 10}}):totable(), {0, 9})
        t.assert_equals(s:update({0}, {{'+', 2, 5}}):totable(), {0, 14})
        t.assert_equals(s:update({0}, {{'+', 2, -4}}):totable(), {0, 10})
        t.assert_equals(s:update({0}, {{'+', 2, -22}}):totable(), {0, -12})
        t.assert_equals(s:update({0}, {{'+', 2, -3}}):totable(), {0, -15})
        t.assert_equals(s:update({0}, {{'+', 2, 7}}):totable(), {0, -8})

        -- '-' with various types
        t.assert_error_msg_equals(
            "Argument type in operation '-' on field 2 does not" ..
            " match field type: expected a number",
            s.update, s, {0}, {{'-', 2, "a"}}
        )
        t.assert_equals(s:update({0}, {{'-', 2, 16}}):totable(), {0, -24})
        t.assert_equals(s:update({0}, {{'-', 2, -4}}):totable(), {0, -20})
        t.assert_equals(s:update({0}, {{'-', 2, -32}}):totable(), {0, 12})
        t.assert_equals(s:update({0}, {{'-', 2, 3}}):totable(), {0, 9})
        t.assert_equals(s:update({0}, {{'-', 2, -5}}):totable(), {0, 14})
        t.assert_equals(s:update({0}, {{'-', 2, 17}}):totable(), {0, -3})

        -- Bitwise ops require unsigned integer
        s:replace{0, 0}
        t.assert_equals(s:update({0}, {{'|', 2, 24}}):totable(), {0, 24})
        t.assert_equals(s:update({0}, {{'|', 2, 2}}):totable(), {0, 26})
        t.assert_equals(s:update({0}, {{'&', 2, 50}}):totable(), {0, 18})
        t.assert_equals(s:update({0}, {{'^', 2, 6}}):totable(), {0, 20})
        t.assert_error_msg_equals(
            "Argument type in operation '|' on field 2 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'|', 2, -1}}
        )
        t.assert_error_msg_equals(
            "Argument type in operation '&' on field 2 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'&', 2, -1}}
        )
        t.assert_error_msg_equals(
            "Argument type in operation '^' on field 2 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'^', 2, -1}}
        )

        -- Signed integer field rejects bitwise ops
        s:replace{0, -1}
        t.assert_error_msg_equals(
            "Argument type in operation '|' on field 2 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'|', 2, 2}}
        )
        t.assert_error_msg_equals(
            "Argument type in operation '&' on field 2 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'&', 2, 40}}
        )
        t.assert_error_msg_equals(
            "Argument type in operation '^' on field 2 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'^', 2, 6}}
        )

        -- Float field rejects bitwise ops
        s:replace{0, 1.5}
        t.assert_error_msg_equals(
            "Argument type in operation '|' on field 2 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'|', 2, 2}}
        )
        t.assert_error_msg_equals(
            "Argument type in operation '&' on field 2 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'&', 2, 40}}
        )
        t.assert_error_msg_equals(
            "Argument type in operation '^' on field 2 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'^', 2, 6}}
        )

        -- Double arithmetic
        s:replace{0, 5}
        t.assert_equals(s:update({0}, {{'+', 2, 1.5}}):totable(), {0, 6.5})
        t.assert_error_msg_equals(
            "Argument type in operation '|' on field 2 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'|', 2, 2}}
        )
        t.assert_equals(s:update({0}, {{'-', 2, 0.5}}):totable(), {0, 6})
        t.assert_equals(s:update({0}, {{'+', 2, 1.5}}):totable(), {0, 7.5})

        -- Float (ffi) arithmetic
        s:replace{0, ffi.new("float", 1.5)}
        t.assert_equals(s:update({0}, {{'+', 2, 2}}):totable(), {0, 3.5})
        t.assert_equals(
            s:update({0}, {{'+', 2, ffi.new("float", 3.5)}}):totable(),
            {0, 7}
        )
        t.assert_error_msg_equals(
            "Argument type in operation '|' on field 2 does not" ..
            " match field type: expected a positive integer",
            s.update, s, {0}, {{'|', 2, 2}}
        )
        t.assert_equals(
            s:update({0}, {{'-', 2, ffi.new("float", 1.5)}}):totable(),
            {0, 5.5}
        )
        t.assert_equals(
            s:update({0}, {{'+', 2, ffi.new("float", 3.5)}}):totable(),
            {0, 9}
        )
        t.assert_equals(
            s:update({0}, {{'-', 2, ffi.new("float", 9)}}):totable(),
            {0, 0}
        )
        -- float + float = ~1.2 (float precision)
        local r = s:update({0}, {{'+', 2, ffi.new("float", 1.2)}})
        t.assert(math.abs(r[2] - 1.2) < 0.001)

        -- Decimal arithmetic
        s:replace{0, decimal.new("2.000")}
        t.assert_equals(
            s:update({0}, {{'+', 2, 2ULL}})[2],
            decimal.new("4.000")
        )
        t.assert_equals(
            s:update({0}, {{'+', 2, -4LL}})[2],
            decimal.new("0.000")
        )
        t.assert_equals(
            s:update({0}, {{'+', 2, 2}})[2],
            decimal.new("2.000")
        )
        t.assert_equals(
            s:update({0}, {{'-', 2, 2}})[2],
            decimal.new("0.000")
        )
        t.assert_equals(
            s:update({0}, {{'-', 2, ffi.new('float', 2)}})[2],
            decimal.new("-2.000")
        )
        t.assert_equals(
            s:update({0}, {{'-', 2, ffi.new('double', 2)}})[2],
            decimal.new("-4.000")
        )
        t.assert_equals(
            s:update({0}, {{'+', 2, decimal.new(4)}})[2],
            decimal.new("0.000")
        )
        t.assert_equals(
            s:update({0}, {{'-', 2, decimal.new(2)}})[2],
            decimal.new("-2.000")
        )

        -- Integer overflow checks
        s:replace{0, 0xfffffffffffffffeull}
        t.assert_equals(
            s:update({0}, {{'+', 2, 1}})[2],
            18446744073709551615ULL
        )
        t.assert_error_msg_equals(
            "Integer overflow when performing '+' operation on field 2",
            s.update, s, {0}, {{'+', 2, 1}}
        )
        t.assert_error_msg_equals(
            "Integer overflow when performing '+' operation on field 2",
            s.update, s, {0}, {{'+', 2, 100500}}
        )
        s:replace{0, 1}
        t.assert_error_msg_equals(
            "Integer overflow when performing '+' operation on field 2",
            s.update, s, {0}, {{'+', 2, 0xffffffffffffffffull}}
        )
        s:replace{0, -1}
        t.assert_equals(
            s:update({0}, {{'+', 2, 0xffffffffffffffffull}})[2],
            18446744073709551614ULL
        )
        s:replace{0, 0}
        t.assert_equals(
            s:update({0}, {{'-', 2, 0x7fffffffffffffffull}})[2],
            -9223372036854775807LL
        )
        s:replace{0, -1}
        t.assert_equals(
            s:update({0}, {{'-', 2, 0x7fffffffffffffffull}})[2],
            -9223372036854775808LL
        )
        s:replace{0, -2}
        t.assert_error_msg_equals(
            "Integer overflow when performing '-' operation on field 2",
            s.update, s, {0}, {{'-', 2, 0x7fffffffffffffffull}}
        )
        s:replace{0, 1}
        t.assert_error_msg_equals(
            "Integer overflow when performing '-' operation on field 2",
            s.update, s, {0}, {{'-', 2, 0xffffffffffffffffull}}
        )
        s:replace{0, 0xffffffffffffffefull}
        t.assert_equals(
            s:update({0}, {{'-', 2, -16}})[2],
            18446744073709551615ULL
        )
        t.assert_error_msg_equals(
            "Integer overflow when performing '-' operation on field 2",
            s.update, s, {0}, {{'-', 2, -16}}
        )
        s:replace{0, -0x4000000000000000LL}
        t.assert_equals(
            s:update({0}, {{'+', 2, -0x4000000000000000LL}})[2],
            -9223372036854775808LL
        )
        s:replace{0, -0x4000000000000000LL}
        t.assert_error_msg_equals(
            "Integer overflow when performing '+' operation on field 2",
            s.update, s, {0}, {{'+', 2, -0x4000000000000001LL}}
        )

        -- Malformed update operation tables
        t.assert_error_msg_equals(
            "Usage: index:update(key, ops)",
            s.update, s, {0}, 0
        )
        t.assert_error_msg_equals(
            "update operation must be an array {op,..}",
            s.update, s, {0}, {'+', 2, 2}
        )
        t.assert_error_msg_equals(
            "update operation must be an array {op,..}, got empty array",
            s.update, s, {0}, {{}}
        )
        t.assert_error_msg_equals(
            "Unknown UPDATE operation #1: wrong number of" ..
            " arguments, expected 3, got 1",
            s.update, s, {0}, {{'+'}})
        t.assert_error_msg_equals(
            "Unknown UPDATE operation #1: wrong number of" ..
            " arguments, expected 3, got 2",
            s.update, s, {0}, {{'+', 0}}
        )
        t.assert_error_msg_equals(
            'Unknown UPDATE operation #1: ""',
            s.update, s, {0}, {{'', 2, 1}}
        )
        t.assert_error_msg_equals(
            'Unknown UPDATE operation #1: "more than 1 character"',
            s.update, s, {0}, {{'more than 1 character', 2, 1}}
        )
        t.assert_error_msg_equals(
            'Unknown UPDATE operation #1: "same as previous"',
            s.update, s, {0}, {{'same as previous'}}
        )
        t.assert_error_msg_equals(
            "Field '+' UPDATE error: invalid JSON in position 1",
            s.update, s, {0}, {{'+', '+', '+'}}
        )
        t.assert_error_msg_equals(
            "update operation name must be a string",
            s.update, s, {0}, {{0, 0, 0}}
        )
    end)
end

-- gh-1142: upsert with many ops must not break WAL.
g.test_gh_1142_upsert_many_ops = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum
        local ops = {}
        for _ = 1, 10 do
            table.insert(ops, {'=', 2, '1234567890'})
        end
        t.assert_equals(s:upsert({0}, ops), nil)
    end)
end

-- gh-1854: empty update returns the tuple unchanged.
g.test_gh_1854_empty_update = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum
        s:insert{0, 'value'}
        t.assert_equals(s:update({0}, {}):totable(), {0, 'value'})
    end)
end

-- gh-2036: msgpackffi must respect the __serialize = 'map' hint.
g.test_gh_2036_serialize_hint = function(cg)
    cg.server:exec(function()
        local s = box.space.tweedledum
        local map = setmetatable({}, {__serialize = 'map'})
        local tup = box.tuple.new({1, 2, 3})
        s:replace({1, 2, 3})

        local r1 = tup:update({{'=', 3, map}})
        t.assert_equals(r1[3], {})

        local r2 = s:update(1, {{'=', 3, map}})
        t.assert_equals(r2[3], {})
    end)
end

-- gh-1261: update by JSON path — basic '!', '=', '#', scalar ops.
g.test_gh_1261_json_path = function(cg)
    cg.server:exec(function()
        local fmt = {
            {'field1', 'unsigned'},
            {'f', 'map'},
            {'g', 'array'},
        }
        local s = box.schema.create_space('test', {format = fmt})
        s:create_index('pk')

        local row = {
            1,
            {
                a = 100, b = 200,
                c = {
                    d = 400, e = 500,
                    f = {4, 5, 6, 7, 8},
                    g = {k = 600, l = 700},
                },
                m = true, g = {800, 900},
            },
            {
                100, 200,
                {{300, 350}, {400, 450}},
                {a = 500, b = 600},
                {c = 700, d = 800},
            },
        }
        local tup = s:insert(row)
        local t4_array = tup:update(
            {{'!', 4, setmetatable({}, {__serialize = 'array'})}}
        )
        local t4_map = tup:update(
            {{'!', 4, setmetatable({}, {__serialize = 'map'})}}
        )

        -- '!' insert: non-intersected paths
        local r = tup:update({
            {'!', 'f.c.f[1]', 3}, {'!', '[3][1]', {100, 200, 300}},
        })
        t.assert_equals(r[2].c.f, {3, 4, 5, 6, 7, 8})
        t.assert_equals(r[3][1], {100, 200, 300})

        r = tup:update({{'!', 'f.g[3]', 1000}})
        t.assert_equals(r[2].g, {800, 900, 1000})

        r = tup:update({{'!', 'g[6]', 'new element'}})
        t.assert_equals(r[3][6], 'new element')

        r = tup:update({{'!', 'f.e', 300}, {'!', 'g[4].c', 700}})
        t.assert_equals(r[2].e, 300)
        t.assert_equals(r[3][4].c, 700)

        r = tup:update({{'!', 'f.c.f[2]', 4.5}, {'!', 'g[3][2][2]', 425}})
        t.assert_equals(r[2].c.f[2], 4.5)
        t.assert_equals(r[3][3][2][2], 425)

        local t2 = tup:update({{'!', 'g[6]', {100}}})
        r = t2:update({{'!', 'g[6][2]', 200}})
        t.assert_equals(r[3][6], {100, 200})
        r = t2:update({{'!', 'g[6][1]', 50}})
        t.assert_equals(r[3][6], {50, 100})

        r = t4_array:update({{'!', '[4][1]', 100}})
        t.assert_equals(r[4][1], 100)
        r = t4_map:update({{'!', '[4].a', 100}})
        t.assert_equals(r[4].a, 100)

        -- '!' error cases
        local ops = {{'!', 'a', 100}}
        t.assert_error_msg_equals(
            "Field 'a' was not found in the tuple",
            tup.update, tup, ops
        )
        t.assert_error_msg_equals(
            "Field 'a' was not found in the tuple",
            tup.upsert, tup, ops
        )

        ops = {{'!', 'f.a', 300}}
        t.assert_error_msg_equals(
            "Field 'f.a' UPDATE error: the key exists already",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].a, 100)

        ops = {{'!', 'f.c.f[0]', 3.5}}
        t.assert_error_msg_equals(
            "Field 'f.c.f[0]' UPDATE error: invalid JSON in position 7",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].c.f[1], 4)

        ops = {{'!', 'f.c.f[100]', 100}}
        t.assert_error_msg_equals(
            "Field ''f.c.f[100]'' was not found in the tuple",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].c.f[1], 4)

        ops = {{'!', 'g[4][100]', 700}}
        t.assert_error_msg_equals(
            "Field 'g[4][100]' UPDATE error:" ..
            " can not access by index a non-array field",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[3][4].a, 500)

        ops = {{'!', 'g[1][1]', 300}}
        t.assert_error_msg_equals(
            "Field 'g[1][1]' UPDATE error:" ..
            " can not access by index a non-array field",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[3][1], 100)

        ops = {{'!', 'f.g.a', 700}}
        t.assert_error_msg_equals(
            "Field 'f.g.a' UPDATE error:" ..
            " can not access by key a non-map field",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].g, {800, 900})

        ops = {{'!', 'f.g[1].a', 700}}
        t.assert_error_msg_equals(
            "Field 'f.g[1].a' UPDATE error:" ..
            " can not access by key a non-map field",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].g[1], 800)

        ops = {{'!', 'f[*].k', 20}}
        t.assert_error_msg_equals(
            "Field 'f[*].k' UPDATE error: invalid JSON in position 3",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].a, 100)

        ops = {{'!', '[2].e.100000', 100}}
        t.assert_error_msg_equals(
            "Field '[2].e.100000' UPDATE error: invalid JSON in position 7",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].a, 100)

        ops = {{'!', '[2].e.f', 100}}
        t.assert_error_msg_equals(
            "Field ''[2].e.f'' was not found in the tuple",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].a, 100)

        -- '=' set: existing and new fields
        r = tup:update({{'=', 'f.a', 150}, {'=', 'g[3][1][2]', 400}})
        t.assert_equals(r[2].a, 150)
        t.assert_equals(r[3][3][1][2], 400)

        r = tup:update({{'=', 'f', {a = 100, b = 200}}})
        t.assert_equals(r[2], {a = 100, b = 200})

        r = tup:update({{'=', 'g[4].b', 700}})
        t.assert_equals(r[3][4].b, 700)

        r = tup:update({{'=', 'f.e', 300}})
        t.assert_equals(r[2].e, 300)

        r = tup:update({{'=', 'f.g[3]', 1000}})
        t.assert_equals(r[2].g[3], 1000)

        r = tup:update({{'=', 'f.g[1]', 0}})
        t.assert_equals(r[2].g[1], 0)

        r = t4_array:update({{'=', '[4][1]', 100}})
        t.assert_equals(r[4][1], 100)

        r = t4_map:update({{'=', '[4]["a"]', 100}})
        t.assert_equals(r[4].a, 100)

        -- '=' error cases
        ops = {{'=', 'f.a[1]', 100}}
        t.assert_error_msg_equals(
            "Field 'f.a[1]' UPDATE error:" ..
            " can not access by index a non-array field",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].a, 100)

        ops = {{'=', 'f.a.k', 100}}
        t.assert_error_msg_equals(
            "Field 'f.a.k' UPDATE error:" ..
            " can not access by key a non-map field",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].a, 100)

        ops = {{'=', 'f.c.f[1]', 100}}
        t.assert_equals(tup:update(ops)[2].c.f[1], 100)
        t.assert_equals(tup:upsert(ops)[2].c.f[1], 100)

        ops = {{'=', 'f.c.f[100]', 100}}
        t.assert_error_msg_equals(
            "Field ''f.c.f[100]'' was not found in the tuple",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].c.f[1], 4)

        ops = {{'=', '[2].c.f 1 1 1 1', 100}}
        t.assert_error_msg_equals(
            "Field '[2].c.f 1 1 1 1' UPDATE error:" ..
            " invalid JSON in position 8",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].c.f[1], 4)

        -- '#' delete via JSON path
        r = tup:update({{'#', '[2].b', 1}})
        t.assert_equals(r[2].b, nil)
        t.assert_equals(r[2].a, 100)

        r = tup:update({{'#', 'f.c.f[1]', 1}})
        t.assert_equals(r[2].c.f, {5, 6, 7, 8})

        r = tup:update({{'#', 'f.c.f[1]', 2}})
        t.assert_equals(r[2].c.f, {6, 7, 8})

        r = tup:update({{'#', 'f.c.f[1]', 100}})
        t.assert_equals(r[2].c.f, {})

        r = tup:update({{'#', 'f.c.f[5]', 1}})
        t.assert_equals(r[2].c.f, {4, 5, 6, 7})

        r = tup:update({{'#', 'f.c.f[5]', 2}})
        t.assert_equals(r[2].c.f, {4, 5, 6, 7})

        -- '#' error cases
        ops = {{'#', 'f.h', 1}}
        t.assert_error_msg_equals(
            "Field ''f.h'' was not found in the tuple",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].a, 100)

        ops = {{'#', 'f.c.f[100]', 1}}
        t.assert_error_msg_equals(
            "Field ''f.c.f[100]'' was not found in the tuple",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].c.f[1], 4)

        ops = {{'#', 'f.b', 2}}
        t.assert_error_msg_equals(
            "Field 'f.b' UPDATE error:" ..
            " can delete only 1 field from a map in a row",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].b, 200)

        ops = {{'#', 'f.b', 0}}
        t.assert_error_msg_equals(
            "Field 'f.b' UPDATE error: cannot delete 0 fields",
            tup.update, tup, ops
        )
        t.assert_error_msg_equals(
            "Field 'f.b' UPDATE error: cannot delete 0 fields",
            tup.upsert, tup, ops
        )

        ops = {{'#', 'f', 0}}
        t.assert_error_msg_equals(
            "Field 'f' UPDATE error: cannot delete 0 fields",
            tup.update, tup, ops
        )
        t.assert_error_msg_equals(
            "Field 'f' UPDATE error: cannot delete 0 fields",
            tup.upsert, tup, ops
        )

        -- Scalar operations via JSON path
        r = tup:update({{'+', 'f.a', 50}})
        t.assert_equals(r[2].a, 150)

        r = tup:update({{'-', 'f.c.f[1]', 0.5}})
        t.assert_equals(r[2].c.f[1], 3.5)

        r = tup:update({{'&', 'f.c.f[2]', 4}})
        t.assert_equals(r[2].c.f[2], 4)

        t2 = tup:update({{'=', 4, {str = 'abcd'}}})
        r = t2:update({{':', '[4].str', 2, 2, 'e'}})
        t.assert_equals(r[4].str, 'aed')

        -- Scalar errors via JSON path
        ops = {{'+', 'g[3]', 50}}
        t.assert_error_msg_equals(
            "Argument type in operation '+' on field 'g[3]'" ..
            " does not match field type: expected a number",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[3][3], {{300, 350}, {400, 450}})

        ops = {{'+', '[2].b.......', 100}}
        t.assert_error_msg_equals(
            "Field '[2].b.......' UPDATE error:" ..
            " invalid JSON in position 7",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].b, 200)

        ops = {{'+', '[2].b.c.d.e', 100}}
        t.assert_error_msg_equals(
            "Field ''[2].b.c.d.e'' was not found in the tuple",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].b, 200)

        ops = {{'-', '[2][*]', 20}}
        t.assert_error_msg_equals(
            "Field '[2][*]' UPDATE error: invalid JSON in position 5",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].a, 100)

        -- Vinyl normalizes field numbers but must not touch JSON paths.
        local vy_fmt = {{'field1', 'unsigned'}, {'field2', 'any'}}
        local vy_s = box.schema.create_space(
            'test2', {engine = 'vinyl', format = vy_fmt}
        )
        vy_s:create_index('pk')
        vy_s:replace(tup)

        box.begin()
        vy_s:upsert({1, 1}, {{'+', 'field2.c.f[1]', 1}})
        vy_s:upsert({1, 1}, {{'+', '[3][3][1][1]', 1}})
        box.commit()

        local res = vy_s:select()[1]
        t.assert_equals(res[2].c.f[1], 5)
        t.assert_equals(res[3][3][1][1], 301)
        vy_s:drop()

        s:drop()
    end)
end

-- gh-1261: complex intersected JSON path updates and nested insertions.
g.test_gh_1261_json_path_complex = function(cg)
    cg.server:exec(function()
        local fmt = {
            {'field1', 'unsigned'},
            {'f', 'map'},
            {'g', 'array'},
        }
        local s = box.schema.create_space('test', {format = fmt})
        s:create_index('pk')

        -- Intersected '=' paths
        local row = {
            1,
            {
                a = 100, b = 200,
                c = {
                    d = 400, e = 500,
                    f = {4, 5, 6, 7, 8},
                    g = {k = 600, l = 700},
                },
                m = true, g = {800, 900},
            },
            {
                100, 200,
                {{300, 350}, {400, 450}},
                {a = 500, b = 600},
                {c = 700, d = 800},
            },
        }
        local tup = s:insert(row)

        local r = tup:update({
            {'=', '[3][3][1][1]', 325}, {'=', '[3][3][1][2]', 375},
        })
        t.assert_equals(r[3][3][1], {325, 375})

        r = tup:update({
            {'=', '[3][3][1][1]', 325}, {'=', '[3][3][2][2]', 475},
        })
        t.assert_equals(r[3][3][1][1], 325)
        t.assert_equals(r[3][3][2][2], 475)

        r = tup:update({
            {'=', '[2].c.f[1]', 4000}, {'=', '[2].c.f[2]', 5000},
        })
        t.assert_equals(r[2].c.f[1], 4000)
        t.assert_equals(r[2].c.f[2], 5000)

        -- Complex tuple with deeper nesting
        local t2 = {1}
        t2[2] = setmetatable({}, {__serialize = 'map'})
        t2[3] = {}
        t2[4] = {
            1, 2, 3,
            {4, 5, 6, 7, {8, 9, {10, 11, 12}, 13, 14}, 15, 16, {17, 18, 19}},
            20, {a = 21, b = 22, c = {d = 23, e = 24}}, 25,
        }
        local tup2 = s:replace(t2)

        r = tup2:update({
            {'=', '[4][4][5][3][2]', 11000}, {'=', '[4][4][8][3]', 19000},
        })
        t.assert_equals(r[4][4][5][3][2], 11000)
        t.assert_equals(r[4][4][8][3], 19000)

        r = tup2:update({
            {'=', '[4][4][3]', 6000}, {'=', '[4][4][5][2]', 9000},
            {'=', '[4][4][8][2]', 18000},
        })
        t.assert_equals(r[4][4][3], 6000)
        t.assert_equals(r[4][4][5][2], 9000)
        t.assert_equals(r[4][4][8][2], 18000)

        r = tup2:update({
            {'=', '[4][3]', 3000}, {'=', '[4][4][2]', 5000},
            {'=', '[4][4][5][1]', 8000},
        })
        t.assert_equals(r[4][3], 3000)
        t.assert_equals(r[4][4][2], 5000)
        t.assert_equals(r[4][4][5][1], 8000)

        r = tup2:update({
            {'=', '[4][4][5][3][2]', 11000}, {'=', '[4][4][5][3][1]', 10000},
            {'=', '[4][4][3]', 6000},
        })
        t.assert_equals(r[4][4][5][3][1], 10000)
        t.assert_equals(r[4][4][5][3][2], 11000)
        t.assert_equals(r[4][4][3], 6000)

        -- Errors: bad JSON ops
        local ops = {{'=', '[4][4][5][1]', 8000}, {'=', '[4][4][5].a', -1}}
        t.assert_error_msg_equals(
            "Field '[4][4][5].a' UPDATE error:" ..
            " can not update array by non-integer index",
            tup2.update, tup2, ops
        )
        t.assert_equals(tup2:upsert(ops)[4][4][5][1], 8000)

        ops = {{'=', '[4][4][5][1]', 8000}, {'=', '[4][4][*].a', -1}}
        t.assert_error_msg_equals(
            "Field '[4][4][*].a' UPDATE error:" ..
            " can not update array by non-integer index",
            tup2.update, tup2, ops
        )
        t.assert_equals(tup2:upsert(ops)[4][4][5][1], 8000)

        ops = {{'=', '[4][4][*].b', 8000}, {'=', '[4][4][*].a', -1}}
        t.assert_error_msg_equals(
            "Field '[4][4][*].b' UPDATE error:" ..
            " invalid JSON in position 8",
            tup2.update, tup2, ops
        )

        ops = {{'=', '[4][4][1]', 4000}, {'=', '[4][4]-badjson', -1}}
        t.assert_error_msg_equals(
            "Field '[4][4]-badjson' UPDATE error:" ..
            " invalid JSON in position 7",
            tup2.update, tup2, ops
        )

        ops = {{'=', '[4][4][1]', 1}, {'=', '[4][4][1]', 2}}
        t.assert_equals(tup2:update(ops)[4][4][1], 2)
        t.assert_equals(tup2:upsert(ops)[4][4][1], 2)

        -- Double set: first op produces bar, second updates same field
        ops = {{'=', '[4][4][1]', 4.5}, {'=', '[4][4][2]', 5.5},
               {'=', '[4][3]', 3.5}}
        r = tup2:update(ops)
        t.assert_equals(r[4][4][1], 4.5)
        t.assert_equals(r[4][4][2], 5.5)
        t.assert_equals(r[4][3], 3.5)

        -- '+' then '+' then '=' bar ops
        ops = {
            {'+', '[4][4][5][4]', 13000},
            {'=', '[4][4][5][1]', 8000},
            {'+', '[4][4][5][4]', 1},
        }
        r = tup2:update(ops)
        t.assert_equals(r[4][4][5][1], 8000)
        t.assert_equals(r[4][4][5][4], 13014)
        t.assert_equals(tup2:upsert(ops)[4][4][5][4], 13014)

        -- '!' complex inserts
        r = tup2:update({
            {'!', '[4][5]', 19.5}, {'!', '[4][6]', 19.75},
            {'!', '[4][4][3]', 5.5}, {'=', '[4][4][2]', 5.25},
        })
        t.assert_equals(r[4][5], 19.5)
        t.assert_equals(r[4][6], 19.75)
        t.assert_equals(r[4][4][3], 5.5)
        t.assert_equals(r[4][4][2], 5.25)

        -- '#' complex deletes
        r = tup2:update({
            {'#', '[4][4][5]', 1}, {'#', '[4][4][4]', 1},
            {'#', '[4][4][5]', 1},
        })
        t.assert_equals(#r[4][4], 5)

        -- Scalar on complex paths
        r = tup2:update({{'+', '[4][3]', 0.5}, {'+', '[4][4][5][5]', 0.75}})
        t.assert_equals(r[4][3], 3.5)
        t.assert_equals(r[4][4][5][5], 14.75)

        r = tup2:update({
            {'+', '[4][3]', 0.5},
            {'+', '[4][4][5][5]', 0.75},
            {'+', '[4][4][3]', 0.25},
        })
        t.assert_equals(r[4][3], 3.5)
        t.assert_equals(r[4][4][5][5], 14.75)
        t.assert_equals(r[4][4][3], 6.25)

        -- Combined: '!' insert followed by '+' on new positions.
        r = tup2:update({
            {'!', '[4][3]', 2.5}, {'+', '[4][5][5][5]', 0.75},
            {'+', '[4][5][3]', 0.25},
        })
        t.assert_equals(r[4][3], 2.5)
        t.assert_equals(r[4][5][5][5], 14.75)
        t.assert_equals(r[4][5][3], 6.25)

        -- Error: following JSON path into scalar field during branching.
        ops = {{'+', '[4][3]', 0.5}, {'+', '[4][3][2]', 0.5}}
        t.assert_error_msg_equals(
            "Field ''[4][3][2]'' was not found in the tuple",
            tup2.update, tup2, ops
        )
        t.assert_equals(tup2:upsert(ops)[4][3], 3.5)

        -- Error: array update with non-numeric index.
        ops = {
            {'=', '[3]', {0}}, {'=', '[4][3]', 3.5},
            {'=', '[4][4][1]', 4.5}, {'=', '[4][4][2]', 5.5},
            {'=', '[4][4].key', 6.5},
        }
        t.assert_error_msg_equals(
            "Field '[4][4].key' UPDATE error:" ..
            " can't update an array by a non-numeric index",
            tup2.update, tup2, ops
        )
        t.assert_equals(tup2:upsert(ops)[3], {0})

        -- Intersecting map updates
        r = tup2:update({
            {'=', '[4][6].c.d', 2300}, {'=', '[4][6].c.e', 2400},
        })
        t.assert_equals(r[4][6].c, {d = 2300, e = 2400})

        r = tup2:update({{'=', '[2].k', 100}, {'=', '[2].v', 200}})
        t.assert_equals(r[2], {k = 100, v = 200})

        s:drop()
    end)
end

-- gh-7705: updating a field that was bar-inserted in the same update.
g.test_gh_7705_bar_insert_update = function(cg)
    cg.server:exec(function()
        local fmt = {
            {'field1', 'unsigned'},
            {'f', 'map'},
            {'g', 'array'},
        }
        local s = box.schema.create_space('test', {format = fmt})
        s:create_index('pk')

        local t2 = {1}
        t2[2] = setmetatable({}, {__serialize = 'map'})
        t2[3] = {}
        t2[4] = {
            1, 2, 3,
            {4, 5, 6, 7, {8, 9, {10, 11, 12}, 13, 14}, 15, 16, {17, 18, 19}},
            20, {a = 21, b = 22, c = {d = 23, e = 24}}, 25,
        }
        local tup2 = s:replace(t2)

        local ops = {
            {'=', '[2].key1', {key11 = 1}},
            {'=', '[2].key1.key12', {key21 = 2}},
        }
        local r = tup2:update(ops)
        t.assert_equals(r[2].key1, {key11 = 1, key12 = {key21 = 2}})
        t.assert_equals(tup2:upsert(ops)[2].key1.key12, {key21 = 2})

        ops = {
            {'=', '[3][1]', {1000}},
            {'=', '[3][1][2]', 2000},
        }
        r = tup2:update(ops)
        t.assert_equals(r[3][1], {1000, 2000})
        t.assert_equals(tup2:upsert(ops)[3][1], {1000, 2000})

        -- Nested bar insertions (4 levels deep map)
        ops = {
            {'=', '[2].key1', {key11 = 1}},
            {'=', '[2].key1.key12', {key21 = 2}},
            {'=', '[2].key1.key12.key22', {key31 = 3}},
            {'=', '[2].key1.key12.key22.key32', 4},
        }
        r = tup2:update(ops)
        t.assert_equals(r[2].key1.key12.key22.key32, 4)
        t.assert_equals(tup2:upsert(ops)[2].key1.key12.key22.key32, 4)

        -- Nested bar insertions (4 levels deep array)
        ops = {
            {'=', '[3][1]', {1000}},
            {'=', '[3][1][2]', {2000, 3000}},
            {'=', '[3][1][2][3]', {4000, 5000, 6000}},
            {'=', '[3][1][2][3][4]', 7000},
        }
        r = tup2:update(ops)
        t.assert_equals(r[3][1][2][3][4], 7000)
        t.assert_equals(tup2:upsert(ops)[3][1][2][3][4], 7000)

        -- Bar second op diverging on field path
        ops = {{'=', '[4][6].f', {g = 1}}, {'=', '[4][6].f.h', {k = 2}}}
        r = tup2:update(ops)
        t.assert_equals(r[4][6].f, {g = 1, h = {k = 2}})
        t.assert_equals(tup2:upsert(ops)[4][6].f.h, {k = 2})

        ops = {
            {'=', '[4][4][5][6]', {1000}},
            {'=', '[4][4][5][6][2]', 2000},
        }
        r = tup2:update(ops)
        t.assert_equals(r[4][4][5][6], {1000, 2000})
        t.assert_equals(tup2:upsert(ops)[4][4][5][6], {1000, 2000})

        ops = {
            {'=', '[4][6].f', {g = {h = 1}}},
            {'=', '[4][6].f.g.k', {l = 2}},
        }
        r = tup2:update(ops)
        t.assert_equals(r[4][6].f.g.k, {l = 2})
        t.assert_equals(tup2:upsert(ops)[4][6].f.g.k, {l = 2})

        ops = {
            {'=', '[4][4][5][6]', {{1000}}},
            {'=', '[4][4][5][6][1][2]', 2000},
        }
        r = tup2:update(ops)
        t.assert_equals(r[4][4][5][6][1], {1000, 2000})
        t.assert_equals(tup2:upsert(ops)[4][4][5][6][1], {1000, 2000})

        ops = {
            {'=', '[4][6].f', {g = {h = 1}}},
            {'=', '[4][6].f.g.k', {l = 2}},
            {'=', '[4][6].f.g.m', {n = 3}},
        }
        r = tup2:update(ops)
        t.assert_equals(r[4][6].f.g.k, {l = 2})
        t.assert_equals(r[4][6].f.g.m, {n = 3})
        t.assert_equals(tup2:upsert(ops)[4][6].f.g.m, {n = 3})

        ops = {
            {'=', '[4][4][5][6]', {{1000}}},
            {'=', '[4][4][5][6][1][2]', 2000},
            {'=', '[4][4][5][6][1][3]', 3000},
        }
        r = tup2:update(ops)
        t.assert_equals(r[4][4][5][6][1], {1000, 2000, 3000})
        t.assert_equals(tup2:upsert(ops)[4][4][5][6][1], {1000, 2000, 3000})

        s:drop()
    end)
end

-- gh-8216: update inside a newly created (bar-inserted) field.
g.test_gh_8216_new_field_update = function(cg)
    cg.server:exec(function()
        local fmt = {
            {'field1', 'unsigned'},
            {'f', 'map'},
            {'g', 'array'},
        }
        local s = box.schema.create_space('test', {format = fmt})
        s:create_index('pk')

        local t2 = {1}
        t2[2] = setmetatable({}, {__serialize = 'map'})
        t2[3] = {}
        t2[4] = {
            1, 2, 3,
            {4, 5, 6, 7, {8, 9, {10, 11, 12}, 13, 14}, 15, 16, {17, 18, 19}},
            20, {a = 21, b = 22, c = {d = 23, e = 24}}, 25,
        }
        local tup2 = s:replace(t2)

        -- Type mismatch: setting array field to map then doing + on map key
        t.assert_error_msg_contains(
            "type does not match",
            tup2.update, tup2, {{'=', '[3]', {a = 33}}, {'+', '[3].a', 1}}
        )

        local r = tup2:update({{'=', '[4]', {1}}, {'+', '[4][1]', 1}})
        t.assert_equals(r[4][1], 2)

        t.assert_error_msg_equals(
            "Field ''[4][6].aa'' was not found in the tuple",
            tup2.update, tup2,
            {{'=', '[4][6]', {a = 33}}, {'+', '[4][6].aa', 1}}
        )

        r = tup2:update({{'=', '[4][4]', {3}}, {'+', '[4][4][1]', 1}})
        t.assert_equals(r[4][4][1], 4)

        s:drop()
    end)
end

-- gh-1261: map-specific update operations with complex key types.
g.test_gh_1261_map_updates = function(cg)
    cg.server:exec(function()
        local tmap = box.tuple.new({
            1,
            {
                a = 1, b = 2, c = {3, 4, 5},
                d = {e = 6, f = 7},
                g = {
                    {k = 8, v = 9},
                    {k = 10, v = 11},
                    {k = '12str', v = '13str'},
                },
                h = {i = {j = 14, k = 15}, m = 16},
            },
            {},
        })

        -- Insert and delete from same map at once
        local r = tmap:update({{'!', '[2].n', 17}, {'#', '[2].b', 1}})
        t.assert_equals(r[2].n, 17)
        t.assert_equals(r[2].b, nil)

        -- Delete a not-existing key is silently ignored
        r = tmap:update({{'=', '[2].d.g', 6000}, {'#', '[2].d.old', 1}})
        t.assert_equals(r[2].d.g, 6000)

        -- Scalar ops on several map fields
        r = tmap:update({
            {'+', '[2].d.e', 1}, {'+', '[2].b', 1}, {'+', '[2].d.f', 1},
        })
        t.assert_equals(r[2].d.e, 7)
        t.assert_equals(r[2].b, 3)
        t.assert_equals(r[2].d.f, 8)

        -- Errors in complex map updates
        local ops = {
            {'=', '[2].h.i.j', 14000}, {'=', '[2].h.i.k', 15000},
            {'=', '[2].h.m', 16000}, {'=', '[2].b', 2000},
            {'+', '[2].h.i.<badjson>', -1},
        }
        t.assert_error_msg_equals(
            "Field '[2].h.i.<badjson>' UPDATE error:" ..
            " invalid JSON in position 9",
            tmap.update, tmap, ops
        )
        t.assert_equals(tmap:upsert(ops)[2].b, 2000)

        ops[5] = {'=', '[2].h.i[20]', -1}
        t.assert_error_msg_equals(
            "Field '[2].h.i[20]' UPDATE error:" ..
            " can't update a map by not a string key",
            tmap.update, tmap, ops
        )
        t.assert_equals(tmap:upsert(ops)[2].b, 2000)

        -- Field not found
        ops = {{'+', '[2].d.e', 1}, {'+', '[2].d.f', 1}, {'+', '[2].d.k', 1}}
        t.assert_error_msg_equals(
            "Field ''[2].d.k'' was not found in the tuple",
            tmap.update, tmap, ops
        )
        t.assert_equals(tmap:upsert(ops)[2].d.e, 7)

        -- Insert nested path where parent doesn't exist
        ops = {{'=', '[2].d.e', 6000}, {'!', '[2].d.g.h', -1}}
        t.assert_error_msg_equals(
            "Field ''[2].d.g.h'' was not found in the tuple",
            tmap.update, tmap, ops
        )
        t.assert_equals(tmap:upsert(ops)[2].d.e, 6000)

        -- Insert an already-existing key
        ops = {{'!', '[2].d.g', 6000}, {'!', '[2].d.e', -1}}
        t.assert_error_msg_equals(
            "Field '[2].d.e' UPDATE error: the key exists already",
            tmap.update, tmap, ops
        )
        t.assert_equals(tmap:upsert(ops)[2].d.g, 6000)

        -- Delete with count > 1 in map
        ops = {{'=', '[2].d.g', 6000}, {'#', '[2].d.old', 10}}
        t.assert_error_msg_equals(
            "Field '[2].d.old' UPDATE error:" ..
            " can delete only 1 field from a map in a row",
            tmap.update, tmap, ops
        )
        t.assert_equals(tmap:upsert(ops)[2].d.g, 6000)

        -- Can't create the whole path
        ops = {{'=', '[2].d.g', 6000}, {'=', '[2].d.new.new', -1}}
        t.assert_error_msg_equals(
            "Field ''[2].d.new.new'' was not found in the tuple",
            tmap.update, tmap, ops
        )
        t.assert_equals(tmap:upsert(ops)[2].d.g, 6000)

        ops = {{'=', '[2].d.g', 6000}, {'#', '[2].d.old.old', 1}}
        t.assert_error_msg_equals(
            "Field ''[2].d.old.old'' was not found in the tuple",
            tmap.update, tmap, ops
        )
        t.assert_equals(tmap:upsert(ops)[2].d.g, 6000)

        t.assert_equals(tmap:upsert(ops)[2].d.g, 6000)
    end)
end

-- gh-4701: arithmetic on float/double must not truncate when result fits float,
-- and must preserve the format declared by the field type.
g.test_gh_4701_float_precision = function(cg)
    cg.server:exec(function()
        local ffi = require('ffi')
        local msgpackffi = require('msgpackffi')
        local mp_array_1 = 0x91
        local mp_double = 0xcb
        local mp_float = 0xca
        local flt_max = 3.402823466e+38
        local uint_max = 18446744073709551615ULL

        local tests = {
            {{'double', 1},       {'double', 1},       mp_float},
            {{'double', flt_max}, {'double', flt_max}, mp_double},
            {{'double', 1},       {'float',  1},       mp_float},
            {{'double', flt_max}, {'float',  flt_max}, mp_double},
            {{'float',  1},       {'float',  1},       mp_float},
            {{'float',  flt_max}, {'float',  flt_max}, mp_double},
            {{'float', -flt_max}, {'float', -flt_max}, mp_double},
            {{'float',  1},       {'int',    1},        mp_float},
            {{'float',  flt_max}, {'uint64_t', uint_max}, mp_float},
            {{'float',  1.0001},  {'double', 1.0000000000001}, mp_double},
        }

        for i, test in ipairs(tests) do
            local val1 = ffi.cast(test[1][1], test[1][2])
            local val2 = ffi.cast(test[2][1], test[2][2])
            local tup = box.tuple.new({val1})
            tup = tup:update({{'+', 1, val2}})
            local m = msgpackffi.encode(tup)
            t.assert_equals(
                m:byte(1), mp_array_1,
                'test ' .. i .. ': expected msgpack array header'
            )
            t.assert_equals(
                m:byte(2), test[3],
                'test ' .. i .. ': unexpected float/double type byte'
            )
        end

        -- 'double' field type must force double encoding even when
        -- value fits float.
        local s = box.schema.create_space('test', {
            format = {
                {'field1', 'unsigned'},
                {'field2', 'double'},
                {'field3', 'any'},
            },
        })
        s:create_index('pk')
        local dbl1 = ffi.cast('double', 1)
        s:replace{1, dbl1, 1}
        local res = s:update({1}, {{'+', 2, dbl1}})
        t.assert_equals(res:totable(), {1, 2, 1})
        s:delete{1}

        -- Deep JSON path must also honour field type.
        s:replace{1, dbl1, {{key1 = {key2 = {1, dbl1}}}}}
        local res2 = s:update({1}, {{'+', 'field3[1].key1.key2[2]', dbl1}})
        t.assert_equals(res2[3][1].key1.key2[2], 2)

        s:drop()
    end)
end

-- gh-6069: reversed-order updates on absent fields must not crash.
g.test_gh_6069_absent_field = function(cg)
    cg.server:exec(function()
        local r = box.tuple.new({1}):update({{'=', 4, 4}, {'=', 3, 3}})
        t.assert_equals(r:totable(), {1, box.NULL, 3, 4})
    end)
end

-- gh-8226: multiple updates targeting the same field in one call.
g.test_gh_8226_multiple_updates = function(cg)
    cg.server:exec(function()
        local decimal = require('decimal')

        -- Deep bar update followed by set in the middle.
        local tup = box.tuple.new({7, {a = {b = {c = {d = 2}}}}, 11})
        local r = tup:update({
            {'+', '[2].a.b.c.d', 2}, {'=', '[2].a.b.c', 7},
        })
        t.assert_equals(r[2].a.b.c, 7)

        -- Double update of a plain array field.
        tup = box.tuple.new({7, 1, 11})
        t.assert_equals(
            tup:update({{'+', 2, 1}, {'+', 2, 2}}):totable(), {7, 4, 11}
        )
        tup = box.tuple.new({7, 1, 11})
        t.assert_equals(
            tup:update({{'+', 2, 2}, {'+', 2, 3}, {'-', 2, 7}}):totable(),
            {7, -1, 11}
        )
        tup = box.tuple.new({7, 1.1, 11})
        t.assert_almost_equals(
            tup:update({{'+', 2, 0.1}, {'+', 2, 0.2}})[2],
            1.4, 1e-10
        )
        tup = box.tuple.new({7, 0.9, 11})
        t.assert_almost_equals(
            tup:update({{'+', 2, 0.2}, {'-', 2, 0.3}})[2],
            0.8, 1e-10
        )

        -- Decimal double updates.
        tup = box.tuple.new({7, decimal.new('1.1000000000000001'), 11})
        t.assert_equals(
            tup:update({{'+', 2, 0.1}, {'+', 2, 0.2}})[2],
            decimal.new('1.4000000000000001')
        )
        tup = box.tuple.new({7, decimal.new('0.9000000000000001'), 11})
        t.assert_equals(
            tup:update({{'+', 2, 0.2}, {'-', 2, 0.3}})[2],
            decimal.new('0.8000000000000001')
        )

        -- Bit operations chained.
        tup = box.tuple.new({7, 0x1, 11})
        t.assert_equals(
            tup:update({{'|', 2, 0x10}, {'|', 2, 0x100}}):totable(),
            {7, 273, 11}
        )
        tup = box.tuple.new({7, 0x111, 11})
        t.assert_equals(
            tup:update({{'&', 2, 0x110}, {'&', 2, 0x101}}):totable(),
            {7, 256, 11}
        )
        tup = box.tuple.new({7, 0x0, 11})
        t.assert_equals(
            tup:update({{'^', 2, 0x1}, {'^', 2, 0x10}}):totable(),
            {7, 17, 11}
        )
        tup = box.tuple.new({7, 0x101, 11})
        t.assert_equals(
            tup:update({{'|', 2, 0x10}, {'&', 2, 0x110}}):totable(),
            {7, 272, 11}
        )

        -- Multiple splice operations on a string field.
        tup = box.tuple.new({7, 'aa11bbb222zzzzz', 11})
        t.assert_equals(
            tup:update({{':', 2, 3, 2, '!'}, {':', 2, 7, 3, '^'}})[2],
            'aa!bbb^zzzzz'
        )
        t.assert_equals(
            tup:update({{':', 2, 3, 2, ''}, {':', 2, 6, 3, ''}})[2],
            'aabbbzzzzz'
        )
        t.assert_equals(
            tup:update({{':', 2, 3, 2, ''}, {':', 2, 9, 0, '++'}})[2],
            'aabbb222++zzzzz'
        )
        t.assert_equals(
            tup:update({{':', 2, 1, 2, 'AA'}, {':', 2, 8, 3, '333'}})[2],
            'AA11bbb333zzzzz'
        )
        t.assert_equals(
            tup:update({{':', 2, 11, 5, 'ZZZZZ'}, {':', 2, 8, 3, '333'}})[2],
            'aa11bbb333ZZZZZ'
        )
        t.assert_equals(
            tup:update({
                {':', 2, 3, 2, '!'}, {':', 2, 7, 3, '^'}, {':', 2, 2, 10, '+'},
            })[2],
            'a+z'
        )

        -- Error: applying scalar op to a map field in the same update.
        tup = box.tuple.new({7, {x = 2}, 11})
        local ops = {{'+', '[2].x', 2}, {'+', 2, 2}}
        t.assert_error_msg_equals(
            "Argument type in operation '+' on field 2 does not" ..
            " match field type: expected a number",
            tup.update, tup, ops
        )
        t.assert_equals(tup:upsert(ops)[2].x, 4)

        -- Double update inside a map.
        tup = box.tuple.new({7, {a = 1, b = 3}, 11})
        r = tup:update({
            {'+', '[2].b', 2}, {'+', '[2].a', 2}, {'+', '[2].a', 3},
        })
        t.assert_equals(r[2].b, 5)
        t.assert_equals(r[2].a, 6)

        -- Error: applying scalar op to a map-typed field in multi-op.
        tup = box.tuple.new({7, {a = {x = 2}, b = 3}, 11})
        ops = {{'+', '[2].b', 2}, {'+', '[2].a.x', 2}, {'+', '[2].a', 2}}
        t.assert_error_msg_equals(
            "Argument type in operation '+' on field '[2].a'" ..
            " does not match field type: expected a number",
            tup.update, tup, ops
        )
        r = tup:upsert(ops)
        t.assert_equals(r[2].a.x, 4)
        t.assert_equals(r[2].b, 5)

        -- Scalar bar: same map key updated twice.
        tup = box.tuple.new({7, {a = 1}, 11})
        t.assert_equals(
            tup:update({{'+', '[2].a', 2}, {'+', '[2].a', 3}})[2].a,
            6
        )

        -- Bar set followed by scalar op.
        tup = box.tuple.new({7, {b = 1}, 11})
        t.assert_equals(
            tup:update({{'=', '[2].b', 2}, {'+', '[2].b', 2}})[2].b,
            4
        )
        t.assert_equals(
            tup:update({{'=', '[2].b', {x = 2}}, {'+', '[2].b.x', 2}})[2].b.x,
            4
        )

        -- Set scalar to array field, then operate on the new value.
        tup = box.tuple.new({7, 1, 11})
        t.assert_equals(
            tup:update({{'=', 2, 2}, {'+', 2, 2}}):totable(),
            {7, 4, 11}
        )
        t.assert_equals(
            tup:update({{'=', 2, {a = 2}}, {'+', '[2].a', 2}})[2].a,
            4
        )
        t.assert_equals(
            tup:update({{'=', 2, {a = 2}}, {'!', '[2].b', 3}})[2].b,
            3
        )
        t.assert_equals(
            tup:update({{'=', 2, {a = 2, b = 3}}, {'#', '[2].b', 1}})[2].b,
            nil
        )
        t.assert_equals(
            tup:update({{'=', 2, {a = 2, b = 3}}, {'=', '[2].b', 4}})[2].b,
            4
        )

        -- Set scalar to map field, then operate on the new value.
        tup = box.tuple.new({7, {a = 1, b = 1}, 11})
        r = tup:update({
            {'+', '[2].a', 1}, {'=', '[2].b', 2}, {'+', '[2].b', 2},
        })
        t.assert_equals(r[2].a, 2)
        t.assert_equals(r[2].b, 4)

        r = tup:update({
            {'+', '[2].a', 1}, {'=', '[2].b', {x = 2}}, {'+', '[2].b.x', 2},
        })
        t.assert_equals(r[2].b.x, 4)

        r = tup:update({
            {'+', '[2].a', 1}, {'=', '[2].b', {x = 2}}, {'!', '[2].b.y', 3},
        })
        t.assert_equals(r[2].b.y, 3)

        r = tup:update({
            {'+', '[2].a', 1},
            {'=', '[2].b', {x = 2, y = 3}},
            {'#', '[2].b.y', 1},
        })
        t.assert_equals(r[2].b.y, nil)

        r = tup:update({
            {'+', '[2].a', 1},
            {'=', '[2].b', {x = 2, y = 3}},
            {'=', '[2].b.y', 4},
        })
        t.assert_equals(r[2].b.y, 4)

        -- '!' to array field then operate on the inserted value.
        tup = box.tuple.new({7, 11})
        t.assert_equals(
            tup:update({{'!', 2, 2}, {'+', 2, 2}}):totable(),
            {7, 4, 11}
        )
        t.assert_equals(
            tup:update({{'!', 2, {a = 2}}, {'+', '[2].a', 2}})[2].a,
            4
        )
        t.assert_equals(
            tup:update({{'!', 2, {a = 2}}, {'!', '[2].b', 3}})[2].b,
            3
        )
        t.assert_equals(
            tup:update({{'!', 2, {a = 2, b = 3}}, {'#', '[2].b', 1}})[2].b,
            nil
        )
        t.assert_equals(
            tup:update({{'!', 2, {a = 2, b = 3}}, {'=', '[2].b', 4}})[2].b,
            4
        )

        -- '!' to map field then operate on the inserted value.
        tup = box.tuple.new({7, {a = 1}, 11})
        r = tup:update({
            {'+', '[2].a', 1}, {'!', '[2].b', 2}, {'+', '[2].b', 2},
        })
        t.assert_equals(r[2].a, 2)
        t.assert_equals(r[2].b, 4)

        r = tup:update({
            {'+', '[2].a', 1}, {'!', '[2].b', {x = 2}}, {'+', '[2].b.x', 2},
        })
        t.assert_equals(r[2].b.x, 4)

        r = tup:update({
            {'+', '[2].a', 1}, {'!', '[2].b', {x = 2}}, {'!', '[2].b.y', 3},
        })
        t.assert_equals(r[2].b.y, 3)

        r = tup:update({
            {'+', '[2].a', 1},
            {'!', '[2].b', {x = 2, y = 3}},
            {'#', '[2].b.y', 1},
        })
        t.assert_equals(r[2].b.y, nil)

        r = tup:update({
            {'+', '[2].a', 1},
            {'!', '[2].b', {x = 2, y = 3}},
            {'=', '[2].b.y', 4},
        })
        t.assert_equals(r[2].b.y, 4)

        -- Remove a field and then re-insert it.
        tup = box.tuple.new({7, 5, 11})
        t.assert_equals(
            tup:update({{'#', 2, 1}, {'!', 2, 6}}):totable(),
            {7, 6, 11}
        )

        tup = box.tuple.new({7, {a = 2, b = 5}, 11})
        r = tup:update({
            {'+', '[2].a', 2}, {'#', '[2].b', 1}, {'!', '[2].b', 6},
        })
        t.assert_equals(r[2].a, 4)
        t.assert_equals(r[2].b, 6)

        r = tup:update({{'#', '[2].b', 1}, {'!', '[2].b', 6}})
        t.assert_equals(r[2].b, 6)

        -- Insert a field and then immediately remove it.
        tup = box.tuple.new({7, 11})
        t.assert_equals(
            tup:update({{'!', 2, 5}, {'#', 2, 1}}):totable(),
            {7, 11}
        )

        tup = box.tuple.new({7, {a = 2}, 11})
        r = tup:update({
            {'+', '[2].a', 2}, {'!', '[2].b', 7}, {'#', '[2].b', 1},
        })
        t.assert_equals(r[2].a, 4)
        t.assert_equals(r[2].b, nil)

        r = tup:update({{'!', '[2].b', 7}, {'#', '[2].b', 1}})
        t.assert_equals(r[2].b, nil)

        -- Bar insert of a non-scalar, then update deep inside it.
        tup = box.tuple.new({7, {a = {a = 2}}, 11})
        r = tup:update({
            {'!', '[2].a.b', {x = {x = {y = 2}}}},
            {'+', '[2].a.b.x.x.y', 2},
        })
        t.assert_equals(r[2].a.b.x.x.y, 4)
    end)
end
