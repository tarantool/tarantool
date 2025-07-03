local json_lib = require('json')
local key_def_lib = require('key_def')

local t = require('luatest')

local g = t.group()

g.test_validate_key = function()
    local key_def = key_def_lib.new({
        {type = 'unsigned', fieldno = 4},
        {type = 'string', fieldno = 2},
    })
    local errmsg

    t.assert_error_msg_equals(
        "Usage: key_def:validate_key(key)",
        key_def.validate_key)
    t.assert_error_msg_equals(
        "Usage: key_def:validate_key(key)",
        key_def.validate_key, key_def)
    t.assert_error_msg_equals(
        "A tuple or a table expected, got number",
        key_def.validate_key, key_def, 0)

    errmsg = "Supplied key type of part 0 does not match index part type: " ..
             "expected unsigned"
    t.assert_error_msg_equals(errmsg, key_def.validate_key, key_def,
                              {'a', 'b'})
    t.assert_error_msg_equals(errmsg, key_def.validate_key, key_def,
                              box.tuple.new({'a', 'b'}))

    errmsg = "Supplied key type of part 1 does not match index part type: " ..
             "expected string"
    t.assert_error_msg_equals(errmsg, key_def.validate_key, key_def,
                              {1, 2})
    t.assert_error_msg_equals(errmsg, key_def.validate_key, key_def,
                              box.tuple.new({1, 2}))

    errmsg = "Invalid key part count (expected [0..2], got 3)"
    t.assert_error_msg_equals(errmsg, key_def.validate_key, key_def,
                              {1, 'a', 1})
    t.assert_error_msg_equals(errmsg, key_def.validate_key, key_def,
                              box.tuple.new({1, 'a', 1}))

    key_def:validate_key({})
    key_def:validate_key({1})
    key_def:validate_key({1, 'a'})
    key_def:validate_key(box.tuple.new({}))
    key_def:validate_key(box.tuple.new({1}))
    key_def:validate_key(box.tuple.new({1, 'a'}))
end

g.test_validate_full_key = function()
    local key_def = key_def_lib.new({
        {type = 'unsigned', fieldno = 4},
        {type = 'string', fieldno = 2},
    })
    local errmsg

    t.assert_error_msg_equals(
        "Usage: key_def:validate_full_key(key)",
        key_def.validate_full_key)
    t.assert_error_msg_equals(
        "Usage: key_def:validate_full_key(key)",
        key_def.validate_full_key, key_def)
    t.assert_error_msg_equals(
        "A tuple or a table expected, got number",
        key_def.validate_full_key, key_def, 0)

    errmsg = "Supplied key type of part 0 does not match index part type: " ..
             "expected unsigned"
    t.assert_error_msg_equals(errmsg, key_def.validate_full_key, key_def,
                              {'a', 'b'})
    t.assert_error_msg_equals(errmsg, key_def.validate_full_key, key_def,
                              box.tuple.new({'a', 'b'}))

    errmsg = "Supplied key type of part 1 does not match index part type: " ..
             "expected string"
    t.assert_error_msg_equals(errmsg, key_def.validate_full_key, key_def,
                              {1, 2})
    t.assert_error_msg_equals(errmsg, key_def.validate_full_key, key_def,
                              box.tuple.new({1, 2}))

    errmsg = "Invalid key part count in an exact match (expected 2, got 1)"
    t.assert_error_msg_equals(errmsg, key_def.validate_full_key, key_def,
                              {1})
    t.assert_error_msg_equals(errmsg, key_def.validate_full_key, key_def,
                              box.tuple.new({1}))

    errmsg = "Invalid key part count in an exact match (expected 2, got 3)"
    t.assert_error_msg_equals(errmsg, key_def.validate_full_key, key_def,
                              {1, 'a', 1})
    t.assert_error_msg_equals(errmsg, key_def.validate_full_key, key_def,
                              box.tuple.new({1, 'a', 1}))

    key_def:validate_full_key({1, 'a'})
    key_def:validate_full_key(box.tuple.new({1, 'a'}))
end

g.test_validate_tuple = function()
    local key_def = key_def_lib.new({
        {type = 'unsigned', fieldno = 4},
        {type = 'string', fieldno = 2},
    })
    local errmsg

    t.assert_error_msg_equals(
        "Usage: key_def:validate_tuple(tuple)",
        key_def.validate_tuple)
    t.assert_error_msg_equals(
        "Usage: key_def:validate_tuple(tuple)",
        key_def.validate_tuple, key_def)
    t.assert_error_msg_equals(
        "A tuple or a table expected, got number",
        key_def.validate_tuple, key_def, 0)

    errmsg = "Supplied key type of part 0 does not match index part type: " ..
             "expected unsigned"
    t.assert_error_msg_equals(errmsg, key_def.validate_tuple, key_def,
                              {box.NULL, 'a', box.NULL, 'b'})
    t.assert_error_msg_equals(errmsg, key_def.validate_tuple, key_def,
                              box.tuple.new({box.NULL, 'a', box.NULL, 'b'}))

    errmsg = "Supplied key type of part 1 does not match index part type: " ..
             "expected string"
    t.assert_error_msg_equals(errmsg, key_def.validate_tuple, key_def,
                              {box.NULL, 1, box.NULL, 2})
    t.assert_error_msg_equals(errmsg, key_def.validate_tuple, key_def,
                              box.tuple.new({box.NULL, 1, box.NULL, 2}))

    errmsg = "Tuple field [4] required by space format is missing"
    t.assert_error_msg_equals(errmsg, key_def.validate_tuple, key_def,
                              {1, 2})
    t.assert_error_msg_equals(errmsg, key_def.validate_tuple, key_def,
                              box.tuple.new({1, 2}))

    key_def:validate_tuple({box.NULL, 'a', box.NULL, 1})
    key_def:validate_tuple(box.tuple.new({box.NULL, 'a', box.NULL, 1}))
end

g.test_compare_keys = function()
    local key_def = key_def_lib.new({
        {type = 'unsigned', fieldno = 4},
        {type = 'string', fieldno = 2},
    })
    local errmsg

    t.assert_error_msg_equals(
        "Usage: key_def:compare_keys(key_a, key_b)",
        key_def.compare_keys)
    t.assert_error_msg_equals(
        "Usage: key_def:compare_keys(key_a, key_b)",
        key_def.compare_keys, key_def)
    t.assert_error_msg_equals(
        "Usage: key_def:compare_keys(key_a, key_b)",
        key_def.compare_keys, key_def, {})
    t.assert_error_msg_equals(
        "A tuple or a table expected, got number",
        key_def.compare_keys, key_def, 0, {})
    t.assert_error_msg_equals(
        "A tuple or a table expected, got number",
        key_def.compare_keys, key_def, {}, 0)

    errmsg = "Supplied key type of part 0 does not match index part type: " ..
             "expected unsigned"
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              {'a', 'b'}, {})
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              box.tuple.new({'a', 'b'}), {})
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              {}, {'a', 'b'})
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              {}, box.tuple.new({'a', 'b'}))

    errmsg = "Supplied key type of part 1 does not match index part type: " ..
             "expected string"
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              {1, 2}, {})
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              box.tuple.new({1, 2}), {})
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              {}, {1, 2})
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              {}, box.tuple.new({1, 2}))

    errmsg = "Invalid key part count (expected [0..2], got 3)"
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              {1, 'a', 1}, {})
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              box.tuple.new({1, 'a', 1}), {})
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              {}, {1, 'a', 1})
    t.assert_error_msg_equals(errmsg, key_def.compare_keys, key_def,
                              {}, box.tuple.new({1, 'a', 1}))

    for _, v in ipairs({
        {{}, {}, 0},
        {{}, {1}, 0},
        {{}, {1, 'a'}, 0},
        {{1}, {}, 0},
        {{1}, {1}, 0},
        {{1}, {1, 'a'}, 0},
        {{1, 'a'}, {}, 0},
        {{1, 'a'}, {1}, 0},
        {{1, 'a'}, {1, 'a'}, 0},
        {{2}, {1}, 1},
        {{2}, {1, 'a'}, 1},
        {{2}, {3}, -1},
        {{2}, {3, 'a'}, -1},
        {{2, 'b'}, {1}, 1},
        {{2, 'b'}, {1, 'a'}, 1},
        {{2, 'b'}, {2, 'a'}, 1},
        {{2, 'b'}, {3}, -1},
        {{2, 'b'}, {3, 'a'}, -1},
        {{2, 'b'}, {2, 'c'}, -1},
    }) do
        local key_a, key_b, ret = unpack(v)
        local msg = string.format('compare(%s, %s)',
                                  json_lib.encode(key_a),
                                  json_lib.encode(key_b))
        t.assert_equals(key_def:compare_keys(key_a, key_b),
                        ret, msg)
        t.assert_equals(key_def:compare_keys(box.tuple.new(key_a), key_b),
                        ret, msg)
        t.assert_equals(key_def:compare_keys(key_a, box.tuple.new(key_b)),
                        ret, msg)
        t.assert_equals(key_def:compare_keys(box.tuple.new(key_a),
                                             box.tuple.new(key_b)),
                        ret, msg)
    end
end

g.test_fixed_point_decimal = function()
    local decimal = require('decimal')

    local errmsg = "scale is not specified for type decimal32"
    -- Creation.
    t.assert_error_msg_equals(errmsg, key_def_lib.new, {
        {type = 'decimal32', fieldno = 2},
    })
    -- Validation.
    local key_def = key_def_lib.new({
        {type = 'decimal32', scale = 3, fieldno = 3},
    })
    key_def:validate_tuple(box.tuple.new({1, 1, decimal.new(1.001)}))
    key_def:validate_key({decimal.new(1.001)})
    key_def:validate_full_key({decimal.new(1.001)})
    local errmsg = "The value of key part does not fit in type"
    t.assert_error_msg_equals(errmsg, key_def.validate_tuple, key_def,
                              box.tuple.new({1, 1, decimal.new(1.0001)}))
    t.assert_error_msg_equals(errmsg, key_def.validate_key, key_def,
                              {decimal.new(1.0001)})
    t.assert_error_msg_equals(errmsg, key_def.validate_full_key, key_def,
                              {decimal.new(1.0001)})
    -- Merging.
    local key_def_1 = key_def_lib.new({
        {type = 'decimal32', scale = 3, fieldno = 3},
    })
    local key_def_2 = key_def_lib.new({
        {type = 'decimal64', scale = 2, fieldno = 4},
    })
    key_def = key_def_1:merge(key_def_2)
    key_def:validate_tuple(box.tuple.new({
        1, 1, decimal.new(1.001), decimal.new(2.02)
    }))
    t.assert_error_msg_equals(
                errmsg, key_def.validate_tuple, key_def,
                box.tuple.new({1, 1, decimal.new(1.0001), decimal.new(2.02)}))
    t.assert_error_msg_equals(
                errmsg, key_def.validate_tuple, key_def,
                box.tuple.new({1, 1, decimal.new(1.001), decimal.new(2.002)}))
    -- Serizalization.
    local key_def = key_def_lib.new({
        {type = 'decimal32', scale = 3, fieldno = 7},
    })
    t.assert_equals(json_lib.decode(json_lib.encode(key_def)), {{
        type = 'decimal32',
        scale = 3,
        fieldno = 7,
        is_nullable = false,
        exclude_null = false,
        sort_order = 'asc',
    }})
end
