local t = require('luatest')
local compat = require('compat')
local console = require('console')
local json = require('json')
local yaml = require('yaml')
local tarantool = require('tarantool')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

-- Test the `prev' argument to the table constructor of `box.error.new'
-- (gh-9103).
g.test_error_new_prev_arg = function()
    local e1 = box.error.new({"errare"})
    local e2 = box.error.new({"humanum", prev = e1})
    local _, e3 = pcall(box.error, {"est", prev = e2})

    t.assert_equals(e3.prev.prev.message, "errare")
    t.assert_equals(e3.prev.message, "humanum")
    t.assert_equals(e3.message, "est")

    t.assert_error_msg_equals(
        "Invalid argument 'prev' (error expected, got number)",
        box.error.new, {prev = 333})
end

-- Test custom error payload (gh-9104).
g.test_custom_payload = function()
    local uuid = require('uuid')
    local decimal = require('decimal')
    local datetime = require('datetime')
    local varbinary = require('varbinary')

    local e = box.error.new({
        reason = "Something happened",
        -- Non-string key is ignored.
        [12] = 34,
        -- Custom payload fields.
        foo = 42,
        bar = "abc",
    })
    -- Check that payload fields can be accessed with the dot operator.
    t.assert_equals(e.foo, 42)
    t.assert_equals(e.bar, "abc")
    -- Ignore the `trace' field, which contains trimmed filename.
    local unpacked = e:unpack()
    unpacked.trace = nil
    t.assert_equals(unpacked, {
        base_type = "ClientError",
        type = "ClientError",
        message = "Something happened",
        code = 0,
        foo = 42,
        bar = "abc",
        name = 'UNKNOWN',
    })

    -- Test `box.error()' with payload fields of various types.
    local my_uuid = uuid.new()
    local _, e = pcall(box.error, {
        type = "MyAppError",
        reason = "Keyboard not found, press F1 to continue",
        num = 42,
        str = "abc",
        arr = {1, 2, 3},
        map = {a = 1, b = 2},
        uuid = my_uuid,
        decimal = decimal.new('1.23'),
        datetime = datetime.new{year = 2000},
        interval = datetime.interval.new{min = 10},
        varbinary = varbinary.new('foo'),
    })
    t.assert_covers(e:unpack(), {
        num = 42,
        str = "abc",
        arr = {1, 2, 3},
        map = {a = 1, b = 2},
        uuid = my_uuid,
        decimal = 1.23,
        datetime = datetime.new{year = 2000},
        interval = datetime.interval.new{min = 10},
        varbinary = varbinary.new('foo'),
    })

    -- Test that built-in error fields can not be overridden.
    e = box.error.new({
        reason = "Non-system disk or disk error",
        code = 0xC0DE,
        base_type = "MyBaseType",
        type = "MyAppError",
        custom_type = "MyCustomType",
        errno = "MyErrno",
        trace = "MyTrace",
    })
    unpacked = e:unpack()
    unpacked.trace = nil
    t.assert_equals(unpacked, {
        message = "Non-system disk or disk error",
        code = 0xC0DE,
        base_type = "CustomError",
        type = "MyAppError",
        custom_type = "MyAppError",
    })
end

-- Test the `message` argument to the table constructor of `box.error.new`
-- (gh-9102).
g.test_error_new_message_arg = function()
    local msg = 'message'
    t.assert_equals(box.error.new{msg}.message, msg)
    t.assert_equals(box.error.new{message = msg}.message, msg)
    t.assert_equals(box.error.new{reason = msg}.message, msg)
    t.assert_equals(box.error.new{msg, message = 'other'}.message, msg)
    t.assert_equals(box.error.new{message = msg, reason = 'other'}.message, msg)
    t.assert_equals(box.error.new{msg, reason = 'other'}.message, msg)
end

-- Test that error's methods are not masked by the payload fields.
g.test_methods_masking = function()
    local e = box.error.new({
        unpack = 'hack',
        raise = 'hack',
        match = 'hack',
        set_prev = 'hack',
        __serialize = 'hack',
    })
    t.assert_type(e.unpack, 'function')
    t.assert_type(e.raise, 'function')
    t.assert_type(e.match, 'function')
    t.assert_type(e.set_prev, 'function')
    t.assert_type(e.__serialize, 'function')
    t.assert_equals(e:unpack().unpack, 'hack')
    t.assert_equals(e:unpack().raise, 'hack')
    t.assert_equals(e:unpack().match, 'hack')
    t.assert_equals(e:unpack().set_prev, 'hack')
    t.assert_equals(e:unpack().__serialize, 'hack')
end

-- Test that payload fields of a cause are inherited by the effect (gh-9106).
g.test_payload_inheritance = function()
    local e1 = box.error.new({foo = 11, bar = 22})
    local e2 = box.error.new({bar = 33, baz = 44, prev = e1})
    local e3 = box.error.new({baz = 55, prev = e2})

    t.assert_equals(e3.foo, 11) -- Inherited from e1.
    t.assert_equals(e3.bar, 33) -- Inherited from e2.
    t.assert_equals(e3.baz, 55) -- Own payload field.
end

g.after_test('test_unpack', function()
    compat.box_error_unpack_type_and_code = 'default'
end)

-- Test that redundant fields of error:unpack() are not serialized (gh-9101).
g.test_unpack = function()
    t.assert(compat.box_error_unpack_type_and_code.default, 'old')

    compat.box_error_unpack_type_and_code = 'old'
    local u = box.error.new{type = 'MyError', reason = 'foobar'}:unpack()
    t.assert_equals(u.base_type, 'CustomError')
    t.assert_equals(u.custom_type, 'MyError')
    t.assert_equals(u.code, 0)
    u = box.error.new(box.error.UNSUPPORTED, 'foo', 'bar'):unpack()
    t.assert_equals(u.base_type, 'ClientError')
    t.assert_equals(u.custom_type, nil)
    t.assert_equals(type(u.code), 'number')

    compat.box_error_unpack_type_and_code = 'new'
    u = box.error.new{type = 'MyError', reason = 'foobar'}:unpack()
    t.assert_equals(u.base_type, nil)
    t.assert_equals(u.custom_type, nil)
    t.assert_equals(u.code, nil)
    u = box.error.new(box.error.UNSUPPORTED, 'foo', 'bar'):unpack()
    t.assert_equals(u.base_type, nil)
    t.assert_equals(u.custom_type, nil)
    t.assert_equals(type(u.code), 'number')
end

local prevprev = box.error.new{reason = "prevprev"}
local prev = box.error.new{reason = "prev", prev = prevprev}
local cur = box.error.new{reason = "cur", prev = prev}

-- Test the increased `box.error` serialization verbosity (gh-9105).
g.test_increased_error_serialization_verbosity = function()
    t.assert_equals(cur:__serialize(), tostring(cur))
    t.assert_equals(yaml.encode(cur), yaml.encode(cur.message))
    t.assert_equals(json.encode(cur), json.encode(cur.message))

    t.assert_equals(compat.box_error_serialize_verbose.default, 'old')
    compat.box_error_serialize_verbose = 'new'

    local prevprev_serialized = prevprev:__serialize()
    local prev_serialized = prev:__serialize()
    local cur_serialized = cur:__serialize()

    t.assert_equals(yaml.encode(cur), yaml.encode(cur_serialized))
    t.assert_equals(json.encode(cur), json.encode(cur_serialized))

    -- Test that serialization of error without `prev` field is equivalent to
    -- `unpack`.
    t.assert_equals(prevprev_serialized, prevprev:unpack())
    -- Test that serialization of `prev` field works correctly.
    t.assert_equals(prev_serialized.prev, prevprev_serialized)
    t.assert_equals(cur_serialized.prev, prev_serialized)
    -- Test that serialization of error with `prev` field is equivalent to
    -- `unpack` except for the `prev` field.
    local prev_unpacked = prev:unpack()
    prev_serialized.prev = nil
    prev_unpacked.prev = nil
    t.assert_equals(prev_serialized, prev_unpacked)
    local cur_unpacked = cur:unpack()
    cur_serialized.prev = nil
    cur_unpacked.prev = nil
    t.assert_equals(cur_serialized, cur_unpacked)
end

g.after_test('test_increased_error_serialization_verbosity', function()
    compat.box_error_serialize_verbose = 'default'
end)

-- Test the increased `box.error` string conversion verbosity (gh-9105).
g.test_increased_error_string_conversion_verbosity = function()
    t.assert_equals(tostring(cur), cur.message)

    t.assert_equals(compat.box_error_serialize_verbose.default, 'old')
    compat.box_error_serialize_verbose = 'new'

    local prevprev_unpacked = prevprev:unpack()
    prevprev_unpacked.message = nil
    prevprev_unpacked = json.encode(prevprev_unpacked)
    t.assert_equals(tostring(prevprev),
                    string.format("%s %s",
                                  prevprev.message, prevprev_unpacked))

    local prev_unpacked = prev:unpack()
    prev_unpacked.message = nil
    prev_unpacked.prev = nil
    prev_unpacked = json.encode(prev_unpacked)
    local cur_unpacked = cur:unpack()
    cur_unpacked.message = nil
    cur_unpacked.prev = nil
    cur_unpacked = json.encode(cur_unpacked)
    t.assert_equals(tostring(cur),
                    string.format("%s %s\n" ..
                                  "%s %s\n" ..
                                  "%s %s",
                                  prevprev.message, prevprev_unpacked,
                                  prev.message, prev_unpacked,
                                  cur.message, cur_unpacked))
end

g.after_test('test_increased_error_string_conversion_verbosity', function()
    compat.box_error_serialize_verbose = 'default'
end)

-- Test ClientError arguments become payload fields (gh-9109).
g.test_client_error_creation = function()
    t.skip_if(not tarantool.build.test_build, 'build is not test one')

    -- We don't check types when add payload from Lua.
    local e = box.error.new(box.error.TEST_TYPE_CHAR, {x = 1, y = {}})
    t.assert_equals(e.field, {x = 1, y = {}})

    -- Test passing not all error payload fields works too.
    local e = box.error.new(box.error.TEST_5_ARGS, 1, 2)
    t.assert_equals(e.f1, 1)
    t.assert_equals(e.f2, 2)
    t.assert_equals(e.f3, nil)
    t.assert_equals(e.f4, nil)
    t.assert_equals(e.f5, nil)

    -- Test passing excess payload fields works too.
    local e = box.error.new(box.error.TEST_TYPE_INT, 1, 2, 3, 4, 5)
    t.assert_equals(e.field, 1)

    -- Test format string is supported in message.
    local e = box.error.new(box.error.TEST_FORMAT_MSG, 1, 'two')
    t.assert_equals(e.message, 'Test error 1 two')
    t.assert_equals(e.f1, 1)
    t.assert_equals(e.f2, 'two')

    -- Test number of arguments of format string may be less
    -- then number of payload arguments.
    local e = box.error.new(box.error.TEST_FORMAT_MSG_FEWER, 1, 'five', 3)
    t.assert_equals(e.message, 'Test error 1 five')
    t.assert_equals(e.f1, 1)
    t.assert_equals(e.f2, 'five')
    t.assert_equals(e.f3, 3)

    -- Test if field name is "" then respective positional argument
    -- is printed in formatted string message but not become payload.
    --
    -- We don't support creation of errors with 'l' modifiers in format string.
    local fields_count = function(e)
        local c = 0
        for _, _ in pairs(e:unpack()) do c = c + 1 end
        return c
    end
    local ref = box.error.new(box.error.TEST_FIRST)
    local e = box.error.new(box.error.TEST_OMIT_TYPE_CHAR, string.byte('x'))
    t.assert_equals(e.message, 'Test error x')
    t.assert_equals(fields_count(e), fields_count(ref))
    local e = box.error.new(box.error.TEST_OMIT_TYPE_INT, 1)
    t.assert_equals(e.message, 'Test error 1')
    t.assert_equals(fields_count(e), fields_count(ref))
    local e = box.error.new(box.error.TEST_OMIT_TYPE_UINT, 2)
    t.assert_equals(e.message, 'Test error 2')
    t.assert_equals(fields_count(e), fields_count(ref))
    local e = box.error.new(box.error.TEST_OMIT_TYPE_STRING, 'str')
    t.assert_equals(e.message, 'Test error str')
    t.assert_equals(fields_count(e), fields_count(ref))
end

--
-- Get tab completion for @s
-- @param s string
-- @return tab completion
local function tabcomplete(s)
    return console.completion_handler(s, 0, #s)
end

-- Test `box.error` autocompletion (gh-9107).
g.test_autocomplete = function()
    -- tabcomplete always uses global table
    rawset(_G, 'err', box.error.new{'cur', foo = 777,
                                    prev = box.error.new{'prev', bar = 777}})

    local r = tabcomplete('err.')
    t.assert_items_equals(r, {
        'err.',
        'err.raise(',
        'err.foo',
        'err.base_type',
        'err.prev',
        'err.message',
        'err.match(',
        'err.set_prev(',
        'err.trace',
        'err.type',
        'err.unpack(',
        'err.code',
        'err.bar',
        'err.name',
    })

    r = tabcomplete('err:')
    t.assert_items_equals(r, {
        'err:',
        'err:match(',
        'err:raise(',
        'err:set_prev(',
        'err:unpack(',
    })
end

-- Test that box.error{code = ..} generates message from errcode (gh-9876).
g.test_error_message_with_named_code_arg = function()
    local code = box.error.TRANSACTION_CONFLICT
    local message = 'Transaction has been aborted by conflict'
    local unk_code = 100500
    local unk_message = 'Unknown error'
    local type = 'ClientError'

    -- Reference behavior.
    t.assert_equals(box.error.new(code).message, message)
    t.assert_equals(box.error.new(code).type, type)
    t.assert_equals(box.error.new(unk_code).message, unk_message)
    t.assert_equals(box.error.new(unk_code).type, type)

    -- Usage of named arguments must lead to similar result.
    t.assert_equals(box.error.new{code = code}.message, message)
    t.assert_equals(box.error.new{code = code}.type, type)
    t.assert_equals(box.error.new{code = unk_code}.message, unk_message)
    t.assert_equals(box.error.new{code = unk_code}.type, type)

    -- By default the code is 0, so it's Unknown error.
    t.assert_equals(box.error.new{}.message, unk_message)
    t.assert_equals(box.error.new{}.type, type)

    -- Explicit specifying or message must have priority.
    t.assert_equals(box.error.new{code = code, reason = 'wtf'}.message, 'wtf')
    t.assert_equals(box.error.new{code = code, reason = 'wtf'}.type, type)
    t.assert_equals(box.error.new{code = code, message = 'wtf'}.message, 'wtf')
    t.assert_equals(box.error.new{code = code, message = 'wtf'}.type, type)

    -- Custom errors are not affected.
    t.assert_equals(box.error.new{type = 'My'}.message, '')
    t.assert_equals(box.error.new{type = 'My'}.type, 'My')
    t.assert_equals(box.error.new{type = 'My', code = code}.message, '')
    t.assert_equals(box.error.new{type = 'My', code = code}.type, 'My')
    t.assert_equals(box.error.new{type = 'My', code = code,
                                  reason = 'wtf'}.message, 'wtf')
    t.assert_equals(box.error.new{type = 'My', code = code,
                                  reason = 'wtf'}.type, 'My')
end

g.before_test('test_tuple_found_payload', function(cg)
    cg.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('primary')
    end)
end)

-- gh-6166: test old and new tuple are available in error object.
g.test_tuple_found_payload = function(cg)
    cg.server:exec(function()
        -- Make MsgPack tuple length larger than TT_STATIC_BUF_LEN
        -- so that tuple in formatted string will be truncated.
        local t1 = {1, string.rep('x', 2048)}
        local t2 = {1, string.rep('y', 2048)}
        box.space.test:insert(t1)
        local ok, actual = pcall(box.space.test.insert, box.space.test, t2)
        t.assert_not(ok)
        actual = actual:unpack()
        t.assert_equals(type(actual), 'table')
        t.assert_str_matches(actual.message,
                             '^Duplicate key exists in unique index.*$')
        actual.message = nil
        t.assert_covers(actual, {
            space = 'test',
            index = 'primary',
            old_tuple = t1,
            new_tuple = t2,
        })
    end)
end

g.after_test('test_tuple_found_payload', function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

-- Test error has 'name' field with error name.
g.test_error_name = function()
    local e = box.error.new(box.error.ILLEGAL_PARAMS, 'foo')
    t.assert_equals(e.name, 'ILLEGAL_PARAMS')
end

g.test_dml_key_validation_errors = function(cg)
    cg.server:exec(function()
        local test = box.schema.create_space('test')
        local index
        test:create_index('primary')

        -- Test non exact key match for non RTREE index.
        index = test:create_index('tree', {
            parts = {1, 'unsigned', 2, 'unsigned'}
        })
        t.assert_error_covers({
            space = 'test',
            index = 'tree',
            space_id = test.id,
            index_id = index.id,
            key = {1, 2, 3},
            message = 'Invalid key part count (expected [0..2], got 3)'
        }, index.select, index, {1, 2, 3})
        t.assert_error_covers({
            space = 'test',
            index = 'tree',
            space_id = test.id,
            index_id = index.id,
            key = {1, 'foo'},
            message = 'Supplied key type of part 1 does not match ' ..
                      'index part type: expected unsigned'
        }, index.select, index, {1, 'foo'})
        index:drop()

        index = test:create_index('hash', {
            type = 'HASH',
            parts = {1, 'unsigned', 2, 'unsigned'}
        })
        t.assert_error_covers({
            space = 'test',
            index = 'hash',
            space_id = test.id,
            index_id = index.id,
            key = {11},
            message = 'HASH index  does not support selects via a partial ' ..
                      'key (expected 2 parts, got 1). Please Consider ' ..
                      'changing index type to TREE.'
        }, index.select, index, {11})
        index:drop()

        -- Test non exact key match for RTREE index.
        index = test:create_index('rtree', {type = 'RTREE',
                                    unique = false,
                                    parts = {2, 'array'}})
        t.assert_error_covers({
            space = 'test',
            index = 'rtree',
            space_id = test.id,
            index_id = index.id,
            key = {1, 2, 3},
            message = 'Invalid key part count (expected [0..4], got 3)'
        }, index.select, index, {1, 2, 3})
        t.assert_error_covers({
            space = 'test',
            index = 'rtree',
            space_id = test.id,
            index_id = index.id,
            key = {'foo'},
            message = 'Supplied key type of part 0 does not match index ' ..
                      'part type: expected array'
        }, index.select, index, {'foo'})
        t.assert_error_covers({
            space = 'test',
            index = 'rtree',
            space_id = test.id,
            index_id = index.id,
            key = {{1, 2, 3}},
            message = 'RTree: Key must be an array with 2 (point) or 4 ' ..
                      '(rectangle/box) numeric coordinates'
        }, index.select, index, {{1, 2, 3}})
        t.assert_error_covers({
            space = 'test',
            index = 'rtree',
            space_id = test.id,
            index_id = index.id,
            key = {{1, 'foo'}},
            message = 'Supplied key type of part 1 does not match index ' ..
                      'part type: expected number'
        }, index.select, index, {{1, 'foo'}})
        t.assert_error_covers({
            space = 'test',
            index = 'rtree',
            space_id = test.id,
            index_id = index.id,
            key = {1, 'foo'},
            message = 'Supplied key type of part 1 does not match index ' ..
                      'part type: expected number'
        }, index.select, index, {1, 'foo'})
        index:drop()

        -- Test exact key match.
        index = test:create_index('secondary', {
            parts = {1, 'unsigned', 2, 'unsigned'}
        })
        t.assert_error_covers({
            space = 'test',
            index = 'secondary',
            space_id = test.id,
            index_id = index.id,
            key = {1, 2, 3},
            message = 'Invalid key part count in an exact match ' ..
                      '(expected 2, got 3)'
        }, index.get, index, {1, 2, 3})
        t.assert_error_covers({
            space = 'test',
            index = 'secondary',
            space_id = test.id,
            index_id = index.id,
            key = {1, 'foo'},
            message = 'Supplied key type of part 1 does not match index ' ..
                      'part type: expected unsigned'
        }, index.get, index, {1, 'foo'})
        index:drop()
        index = test:create_index('secondary', {
            parts = {1, 'unsigned', 2, 'unsigned'},
            unique = false,
        })
        t.assert_error_covers({
            space = 'test',
            index = 'secondary',
            space_id = test.id,
            index_id = index.id,
            message = "Get() doesn't support partial keys and non-unique " ..
                      "indexes"
        }, index.delete, index, {1, 2})
    end)
end

g.after_test('test_dml_key_validation_errors', function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_dml_tuple_validation_errors = function(cg)
    cg.server:exec(function()

        -- Test simple tuple validation errors.
        local test = box.schema.create_space('test', {field_count = 5})
        test:create_index('primary')
        t.assert_error_covers({
            tuple = {1, 2},
            message = 'Tuple field count 2 does not match space field count 5'
        }, test.insert, test, {1, 2})
        test:drop()
        local test = box.schema.create_space('test', {format = {
            {'f1', 'number'}, {'f2', 'number'}, {'f3', 'number'},
        }})
        test:create_index('primary')
        t.assert_error_covers({
            tuple = {3, 4},
            message = 'Tuple field 3 (f3) required by space format is missing'
        }, test.insert, test, {3, 4})
        t.assert_error_covers({
            tuple = {5, 'foo', 6},
            message = 'Tuple field 2 (f2) type does not match one required ' ..
                      'by operation: expected number, got string'
        }, test.insert, test, {5, 'foo', 6})
        test:drop()
        local test = box.schema.create_space('test', {format = {
            {'f1', 'number'}, {'f2', 'int8'},
        }})
        test:create_index('primary')
        t.assert_error_covers({
            tuple = {7, 300},
            message = "The value of field 2 (f2) exceeds the supported " ..
                       "range for type 'int8': expected [-128..127], got 300"
        }, test.insert, test, {7, 300})
        test:drop()

        -- Test foreign key constraint errors.
        local foreign = box.schema.create_space('foreign', {format = {
            {'id', 'number'},
            {'extra_id', 'number'},
        }})
        foreign:create_index('primary')
        foreign:create_index('secondary', {parts = {1, 'number', 2, 'number'}})
        local test = box.schema.create_space('test', {format = {
            {name = 'id', type = 'number'},
            {name = 'link', foreign_key = {space = 'foreign', field = 'id'}}
        }})
        t.assert_error_covers({
            tuple = {1, 2},
            message = "Foreign key constraint 'foreign' failed for field " ..
                      "'2 (link)': foreign tuple was not found",
        }, test.insert, test, {1, 2})
        test:drop()
        local test = box.schema.create_space('test', {
            format = {{'foreign_id', 'number'}, {'foreign_extra_id', 'number'}},
            foreign_key = {
                space = 'foreign',
                field = {foreign_id = 'id', foreign_extra_id = 'extra_id'},
            },
        })
        t.assert_error_covers({
            tuple = {3, 4},
            message = "Foreign key constraint 'foreign' failed: foreign " ..
                      "tuple was not found",
        }, test.insert, test, {3, 4})
        test:drop()
        foreign:drop()

        -- Test tuple/fields constraint errors.
        box.schema.func.create('some_constraint', {
            language = 'LUA', is_deterministic = true,
            body = [[function(_, v) return v < 10 end]],
        })
        local test = box.schema.create_space('test', {format = {
            {name = 'id', type = 'number'},
            {name = 'value', type = 'number', constraint = 'some_constraint'}
        }})
        t.assert_error_covers({
            tuple = {1, 11},
            message = "Check constraint 'some_constraint' failed for field " ..
                      "'2 (value)'"
        }, test.insert, test, {1, 11})
        test:drop()
        box.schema.func.drop('some_constraint')
        box.schema.func.create('some_constraint', {
            language = 'LUA', is_deterministic = true,
            body = [[function(t) return t[1] ~= t[2] end]],
        })
        local test = box.schema.create_space('test', {
            format = {
                {name = 'id', type = 'number'},
                {name = 'value', type = 'number'},
            },
            constraint = 'some_constraint',
        })
        t.assert_error_covers({
            tuple = {7, 7},
            message = "Check constraint 'some_constraint' failed for a tuple",
        }, test.insert, test, {7, 7})
        test:drop()
        box.schema.func.drop('some_constraint')

        -- Test some errors when format has JSON path spec.
        local test = box.schema.create_space('test', {field_count = 5})
        foreign:create_index('primary')
        test:create_index('secondary', {parts = {
            {1, 'number'}, {2, 'str', path = 'x.y'}
        }})
        t.assert_error_covers({
            tuple = {1, {x = {y = 2}}},
            message = 'Tuple field count 2 does not match space field count 5',
        }, test.insert, test, {1, {x = {y = 2}}})
        test:drop()
        local test = box.schema.create_space('test')
        foreign:create_index('primary')
        test:create_index('secondary', {parts = {
            {1, 'number'}, {2, 'str', path = 'x.y'}
        }})
        t.assert_error_covers({
            tuple = {3, {x = {z = 4}}},
            message = 'Tuple field [2]["x"]["y"] required by space ' ..
                      'format is missing',
        }, test.insert, test, {3, {x = {z = 4}}})
        test:drop()
    end)
end

g.after_test('test_dml_tuple_validation_errors', function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        if box.space.foreign ~= nil then
            box.space.foreign:drop()
        end
        box.schema.func.drop('some_constraint', {if_exists = true})
    end)
end)

local g_engine = t.group('engine', {{engine = 'memtx'}, {engine = 'vinyl'}})

g_engine.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g_engine.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

g_engine.test_dml_tuple_validation_errors_space_field = function(cg)
    cg.server:exec(function(params)
        local test = box.schema.create_space('test', {
            format = {{'f1', 'unsigned'}, {'f2', 'unsigned'}},
            engine = params.engine,
        })
        test:create_index('primary')
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            message = 'Tuple field 2 (f2) type does not match one required ' ..
                      'by operation: expected unsigned, got string',
        }, test.insert, test, {1, 'foo'})
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            message = 'Tuple field 2 (f2) type does not match one required ' ..
                      'by operation: expected unsigned, got string',
        }, test.replace, test, {1, 'foo'})
        test:replace({1, 2})
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            index = 'primary',
            index_id = 0,
            message = 'Tuple field 2 (f2) type does not match one required ' ..
                      'by operation: expected unsigned, got string',
        }, test.update, test, 1, {{'=', 2, 'foo'}})

    end, {cg.params})
end

g_engine.after_test('test_dml_tuple_validation_errors_space_field', function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_dml_tuple_validation_errors_space_field_upsert = function(cg)
    cg.server:exec(function()
        local test = box.schema.create_space('test', {
            format = {{'f1', 'unsigned'}, {'f2', 'unsigned'}},
        })
        test:create_index('primary')
        test:replace({1, 2})
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            message = 'Tuple field 2 (f2) type does not match one required ' ..
                      'by operation: expected unsigned, got string',
        }, test.upsert, test, {1, 3}, {{'=', 2, 'foo'}})
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            message = 'Tuple field 2 (f2) type does not match one required ' ..
                      'by operation: expected unsigned, got string',
        }, test.upsert, test, {2, 'foo'}, {{'=', 2, 4}})
        test:drop()
    end)
end

g.after_test('test_dml_tuple_validation_errors_space_field_upsert', function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_dml_update_primary_key = function(cg)
    cg.server:exec(function()
        local test = box.schema.create_space('test')
        test:create_index('primary')
        test:replace({1, 2})
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            old_tuple = {1, 2},
            new_tuple = {3, 2},
            message = "Attempt to modify a tuple field which is part of " ..
                      "primary index in space 'test'",
        }, test.update, test, {1}, {{'=', 1, 3}})

        local update_pk = function(_, new)
            return box.tuple.update(new, {{'+', 1, 1}})
        end
        test:before_replace(update_pk)
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            old_tuple = {1, 2},
            new_tuple = {2, 3},
            message = "Attempt to modify a tuple field which is part of " ..
                      "primary index in space 'test'",
        }, test.replace, test, {1, 3})
        test:before_replace(nil, update_pk)
        test.index.primary:drop()

        test:create_index('primary', {type = 'HASH'})
        test:replace({1, 2})
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            old_tuple = {1, 2},
            new_tuple = {3, 2},
            message = "Attempt to modify a tuple field which is part of " ..
                      "primary index in space 'test'",
        }, test.update, test, {1}, {{'=', 1, 3}})
        test:drop()

        local space = box.space._session_settings
        space:update('sql_full_metadata', {{'=', 2, false}})
        t.assert_error_covers({
            space = '_session_settings',
            space_id = space.id,
            old_tuple = {'sql_full_metadata', false},
            new_tuple = {'foo', false},
            message = "Attempt to modify a tuple field which is part of " ..
                      "primary index in space '_session_settings'",
        }, space.update, space, 'sql_full_metadata', {{'=', 1, 'foo'}})

        local test = box.schema.create_space('test', {engine = 'vinyl'})
        test:create_index('primary')
        test:replace({1, 2})
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            old_tuple = {1, 2},
            new_tuple = {3, 2},
            message = "Attempt to modify a tuple field which is part of " ..
                      "primary index in space 'test'",
        }, test.update, test, {1}, {{'=', 1, 3}})
    end)
end

g.after_test('test_dml_update_primary_key', function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_dml_update_error = function(cg)
    cg.server:exec(function()
        local test = box.schema.create_space('test')
        test:create_index('primary')
        test:replace({1, 2})
        local ops = {}
        for i = 1,5000 do ops[i] = {'+', 2, 1} end
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            index = 'primary',
            index_id = 0,
            ops = ops,
            message = 'Illegal parameters, too many operations for update'
        }, test.update, test, {1}, ops)
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            index = 'primary',
            index_id = 0,
            ops = {{'+', 2, 'foo'}},
            message = "Argument type in operation '+' on field 2 does not " ..
                      "match field type: expected a number",
        }, test.update, test, {1}, {{'+', 2, 'foo'}})
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            index = 'primary',
            index_id = 0,
            ops = {{':', 2, 2, 1, 'foo'}},
            tuple = {1, 2},
            message = "Argument type in operation ':' on field 2 does not " ..
                      "match field type: expected a string or varbinary"
        }, test.update, test, {1}, {{':', 2, 2, 1, 'foo'}})

        local dummy = function(_, new) return new end
        test:before_replace(dummy)
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            index = 'primary',
            index_id = 0,
            ops = {{'+', 2, 'foo'}},
            message = "Argument type in operation '+' on field 2 does not " ..
                      "match field type: expected a number"
        }, test.update, test, {1}, {{'+', 2, 'foo'}})
        test:before_replace(nil, dummy)
        test:drop()

        local space = box.space._session_settings
        space:update('sql_full_metadata', {{'=', 2, false}})
        t.assert_error_covers({
            space = '_session_settings',
            space_id = space.id,
            index = 'primary',
            index_id = 0,
            ops = {{'+', 2, 'foo'}},
            message = "Argument type in operation '+' on field 2 does not " ..
                      "match field type: expected a number"
        }, space.update, space, 'sql_full_metadata', {{'+', 2, 'foo'}})

        local test = box.schema.create_space('test', {engine = 'vinyl'})
        test:create_index('primary')
        test:replace({1, 2})
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            index = 'primary',
            index_id = 0,
            ops = {{'+', 2, 'foo'}},
            message = "Argument type in operation '+' on field 2 does not " ..
                      "match field type: expected a number",
        }, test.update, test, {1}, {{'+', 2, 'foo'}})
    end)
end

g.after_test('test_dml_update_error', function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_dml_upsert_error = function(cg)
    cg.server:exec(function()
        local test = box.schema.create_space('test', {engine = 'vinyl'})
        test:create_index('primary')
        test:replace({1, 2})
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            ops = {{'=', 7}},
            message = 'Unknown UPDATE operation #1: wrong number of ' ..
                      'arguments, expected 3, got 2',
        }, test.upsert, test, {5}, {{'=', 7}})

        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            ops = {{'=', 5}},
            message = 'Unknown UPDATE operation #1: wrong number of ' ..
                      'arguments, expected 3, got 2',
        }, test.upsert, test, {1}, {{'=', 5}})

        local dummy = function(_, new) return new end
        test:before_replace(dummy)
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            ops = {{'=', 7}},
            message = 'Unknown UPDATE operation #1: wrong number of ' ..
                      'arguments, expected 3, got 2',
        }, test.upsert, test, {5}, {{'=', 7}})

        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            ops = {{'=', 5}},
            message = 'Unknown UPDATE operation #1: wrong number of ' ..
                      'arguments, expected 3, got 2',
        }, test.upsert, test, {1}, {{'=', 5}})
        test:before_replace(nil, dummy)
        test:drop()

        local test = box.schema.create_space('test', {engine = 'vinyl'})
        test:create_index('primary')
        test:replace({1, 2})
        t.assert_error_covers({
            space = 'test',
            space_id = test.id,
            ops = {{'=', 7}},
            message = 'Unknown UPDATE operation #1: wrong number of ' ..
                      'arguments, expected 3, got 2',
        }, test.upsert, test, {5}, {{'=', 7}})
    end)
end

g.after_test('test_dml_upsert_error', function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_error_is = function()
    t.assert(box.error.is(box.error.new(box.error.UNKNOWN)))
    t.assert(box.error.is(box.error.new(box.error.UNKNOWN)), 'foo')

    t.assert_not(box.error.is(nil))
    t.assert_not(box.error.is(1))
    t.assert_not(box.error.is('foo'))
    t.assert_not(box.error.is('foo'), 'bar')
    t.assert_not(box.error.is(box.tuple.new({1, 'foo'})))
end
