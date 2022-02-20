#!/usr/bin/env tarantool

local yaml = require('yaml').new()
yaml.cfg{
    encode_invalid_numbers = true,
    encode_load_metatables = true,
    encode_use_tostring    = true,
    encode_invalid_as_nil  = true,
}
local test = require('tap').test('table')
test:plan(49)

do -- check basic table.copy (deepcopy)
    local example_table = {
        {1, 2, 3},
        {"help, I'm very nested", {{{ }}} }
    }

    local copy_table = table.deepcopy(example_table)

    test:is_deeply(
        example_table,
        copy_table,
        "checking, that deepcopy behaves ok"
    )
    test:isnt(
        example_table,
        copy_table,
        "checking, that tables are different"
    )
    test:isnt(
        example_table[1],
        copy_table[1],
        "checking, that tables are different"
    )
    test:isnt(
        example_table[2],
        copy_table[2],
        "checking, that tables are different"
    )
    test:isnt(
        example_table[2][2],
        copy_table[2][2],
        "checking, that tables are different"
    )
    test:isnt(
        example_table[2][2][1],
        copy_table[2][2][1],
        "checking, that tables are different"
    )
end

do -- check basic table.copy (deepcopy)
    local example_table = {
        {1, 2, 3},
        {"help, I'm very nested", {{{ }}} }
    }

    local copy_table = table.copy(example_table, true)

    test:is_deeply(
        example_table,
        copy_table,
        "checking, that deepcopy behaves ok + shallow"
    )
    test:isnt(
        example_table,
        copy_table,
        "checking, that tables are different + shallow"
    )
    test:is(
        example_table[1],
        copy_table[1],
        "checking, that tables are the same + shallow"
    )
    test:is(
        example_table[2],
        copy_table[2],
        "checking, that tables are the same + shallow"
    )
    test:is(
        example_table[2][2],
        copy_table[2][2],
        "checking, that tables are the same + shallow"
    )
    test:is(
        example_table[2][2][1],
        copy_table[2][2][1],
        "checking, that tables are the same + shallow"
    )
end

do -- check cycle resolution for table.copy (deepcopy)
    local recursive_table_1 = {}
    local recursive_table_2 = {}

    recursive_table_1[1] = recursive_table_2
    recursive_table_2[1] = recursive_table_1

    local copy_table_1 = table.deepcopy(recursive_table_1)
    local copy_table_2 = table.deepcopy(recursive_table_2)

    test:isnt(
        copy_table_1,
        recursive_table_1,
        "table 1. checking, that tables are different"
    )
    test:isnt(
        copy_table_1[1],
        recursive_table_1[1],
        "table 1. checking, that tables are different"
    )
    test:isnt(
        copy_table_1[1][1],
        recursive_table_1[1][1],
        "table 1. checking, that tables are different"
    )
    test:is(
        copy_table_1,
        copy_table_1[1][1],
        "table 1. checking, that cyclic reference is ok"
    )

    test:isnt(
        copy_table_2,
        recursive_table_2,
        "table 2. checking, that tables are different"
    )
    test:isnt(
        copy_table_2[1],
        recursive_table_2[1],
        "table 2. checking, that tables are different"
    )
    test:isnt(
        copy_table_2[1][1],
        recursive_table_2[1][1],
        "table 2. checking, that tables are different"
    )
    test:is(
        copy_table_2,
        copy_table_2[1][1],
        "table 2. checking, that cyclic reference is ok"
    )
end

do -- check usage of __copy metamethod
    local copy_mt = nil; copy_mt = {
        __copy = function(self)
            local new_self = { a = 1}
            return setmetatable(new_self, copy_mt)
        end
    }
    local one_self = setmetatable({ a = 2 }, copy_mt)
    local another_self = table.deepcopy(one_self)

    test:isnt(one_self, another_self, "checking that output tables differs")
    test:is(
        getmetatable(one_self),
        getmetatable(another_self),
        "checking that we've called __copy"
    )
    test:isnt(one_self.a, another_self.a, "checking that we've called __copy")
end

do -- check usage of __copy metamethod + shallow
    local copy_mt = nil; copy_mt = {
        __copy = function(self)
            local new_self = { a = 1}
            return setmetatable(new_self, copy_mt)
        end
    }
    local one_self = setmetatable({ a = 2 }, copy_mt)
    local another_self = table.copy(one_self, true)

    test:isnt(
        one_self,
        another_self,
        "checking that output objects differs + shallow"
    )
    test:is(
        getmetatable(one_self),
        getmetatable(another_self),
        "checking that we've called __copy + shallow (same obj types)"
    )
    test:isnt(
        one_self.a,
        another_self.a,
        "checking that we've called __copy + shallow (diff obj values)"
    )
end

do -- check usage of not __copy metamethod on second level + shallow
    local copy_mt = nil; copy_mt = {
        __copy = function(self)
            local new_self = { a = 1 }
            return setmetatable(new_self, copy_mt)
        end
    }
    local one_self = { setmetatable({ a = 2 }, copy_mt) }
    local another_self = table.copy(one_self, true)

    test:isnt(
        one_self, another_self,
        "checking that output tables differs + shallow"
    )
    test:isnil(
        getmetatable(one_self),
        "checking that we've called __copy + shallow and no mt"
    )
    test:isnil(
        getmetatable(another_self),
        "checking that we've called __copy + shallow and no mt"
    )
    test:is(
        one_self[1],
        another_self[1],
        "checking that we've called __copy + shallow and object is the same"
    )
    test:is(
        one_self[1].a,
        another_self[1].a,
        "checking that we've called __copy + shallow and object val is the same"
    )
end

do -- check table.equals
    test:ok(table.equals({}, {}), "table.equals for empty tables")
    test:is(table.equals({}, {1}), false, "table.equals with one empty table")
    test:is(table.equals({1}, {}), false, "table.equals with one empty table")
    test:is(table.equals({key = box.NULL}, {key = nil}), false,
            "table.equals for box.NULL and nil")
    test:is(table.equals({key = nil}, {key = box.NULL}), false,
            "table.equals for box.NULL and nil")
    local tbl_a = {
        first = {
            1,
            2,
            {},
        },
        second = {
            a = {
                {'something'},
            },
            b = 'something else',
        },
        [3] = 'some value',
    }
    local tbl_b = table.deepcopy(tbl_a)
    local tbl_c = table.copy(tbl_a)
    test:ok(table.equals(tbl_a, tbl_b), "table.equals for complex tables")
    test:ok(table.equals(tbl_a, tbl_c),
            "table.equals for shallow copied tables")
    tbl_c.second.a = 'other thing'
    test:ok(table.equals(tbl_a, tbl_c),
            "table.equals for shallow copied tables after modification")
    test:is(table.equals(tbl_a, tbl_b), false, "table.equals does a deep check")
    local mt = {
        __eq = function(a, b) -- luacheck: no unused args
            return true
        end}
    local tbl_d = setmetatable({a = 15}, mt)
    local tbl_e = setmetatable({b = 2, c = 3}, mt)
    test:ok(table.equals(tbl_d, tbl_e), "table.equals respects __eq")
    test:is(table.equals(tbl_d, {a = 15}), false,
            "table.equals when metatables don't match")
    test:is(table.equals({a = 15}, tbl_d), false,
            "table.equals when metatables don't match")
    local tbl_f = setmetatable({a = 15}, {__eq = function() return true end})
    test:is(table.equals(tbl_d, tbl_f), false,
            "table.equals when metatables don't match")
end

do -- check table.equals with booleans (gh-6386)
    test:is(table.equals({a = false}, {a = false}), true,
            "table.equals when booleans are used")
    test:is(table.equals({a = false}, {}), false,
            "table.equals when booleans are used")
    test:is(table.equals({}, {a = false}), false,
            "table.equals when booleans are used")
    test:is(table.equals({a = box.NULL}, {a = false}), false,
            "table.equals when booleans are used")
    test:is(table.equals({a = false}, {a = box.NULL}), false,
            "table.equals when booleans are used")
end

os.exit(test:check() == true and 0 or 1)
