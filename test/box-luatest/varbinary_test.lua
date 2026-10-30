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

--
-- gh-4201: Introduce varbinary field type.
--
g.test_field_type = function(cg)
    cg.server:exec(function()
        local varbinary = require('varbinary')
        local s = box.schema.space.create('withdata')
        s:format({{"b", "integer"}})
        t.assert_error_msg_equals(
            "Field 1 (b) has type 'integer' in space format, " ..
            "but type 'varbinary' in index definition",
            s.create_index, s, 'pk', {parts = {1, "varbinary"}})
        s:format({{"b", "varbinary"}})
        t.assert_error_msg_equals(
            "Field 1 (b) has type 'varbinary' in space format, " ..
            "but type 'integer' in index definition",
            s.create_index, s, 'pk', {parts = {1, "integer"}})
        local pk = s:create_index('pk', {parts = {1, "varbinary"}})
        s:insert({varbinary.new('\xDE\xAD\xBE\xAF')})
        s:insert({varbinary.new('\xFE\xED\xFA\xCE')})
        local result = s:select()
        t.assert_equals(result, {
            {'\xDE\xAD\xBE\xAF'},
            {'\xFE\xED\xFA\xCE'},
        })
        t.assert(varbinary.is(result[1].b))
        t.assert(varbinary.is(result[2].b))
        result = box.execute("SELECT * FROM \"withdata\" " ..
                             "WHERE \"b\" < x'FEEDFACE';")
        t.assert_equals(result, {
            metadata = {
                {name = 'b', type = 'varbinary'},
            },
            rows = {
                {'\xDE\xAD\xBE\xAF'},
            },
        })
        t.assert(varbinary.is(result.rows[1][1]))
        pk:alter({parts = {1, 'scalar'}})
        s:format({{'b', 'scalar'}})
        s:insert({11})
        s:insert({22})
        s:insert({'11'})
        s:insert({'22'})
        t.assert_equals(s:select(), {
            {11},
            {22},
            {'11'},
            {'22'},
            {'\xDE\xAD\xBE\xAF'},
            {'\xFE\xED\xFA\xCE'},
        })
        t.assert_equals(box.execute("SELECT * FROM \"withdata\" " ..
                                    "WHERE \"b\" <= x'DEADBEAF';"), {
            metadata = {
                {name = 'b', type = 'scalar'},
            },
            rows = {
                {11},
                {22},
                {'11'},
                {'22'},
                {'\xDE\xAD\xBE\xAF'},
            },
        })
        t.assert_error_msg_equals(
            "Tuple field 1 (b) type does not match one " ..
            "required by operation: expected varbinary, got unsigned",
            pk.alter, pk, {parts = {1, 'varbinary'}})
        s:delete({11})
        s:delete({22})
        s:delete({'11'})
        s:delete({'22'})
        s:insert({varbinary.new('\xFA\xDE\xDE\xAD')})
        pk:alter({parts = {1, 'varbinary'}})
        t.assert_equals(s:select(), {
            {'\xDE\xAD\xBE\xAF'},
            {'\xFA\xDE\xDE\xAD'},
            {'\xFE\xED\xFA\xCE'},
        })
    end)
end

g.after_test('test_field_type', function(cg)
    cg.server:exec(function()
        if box.space.withdata then
            box.space.withdata:drop()
        end
    end)
end)

--
-- gh-5071: Bitset index for binary fields.
--
g.test_bitset_index = function(cg)
    cg.server:exec(function()
        local varbinary = require('varbinary')
        local s = box.schema.space.create('withdata')
        s:create_index('pk', {parts = {1, "varbinary"}})
        local bs = s:create_index('bitset', {type = 'bitset',
                                             parts = {1, 'varbinary'}})
        s:insert({varbinary.new('\xDE\xAD\xBE\xAF')})
        s:insert({varbinary.new('\xFA\xDE\xDE\xAD')})
        s:insert({varbinary.new('\xFE\xED\xFA\xCE')})
        s:insert({varbinary.new('\xFF')})
        t.assert_equals(bs:select(varbinary.new('\xFF'), 'BITS_ALL_SET'), {
            {'\xFF'},
        })
        t.assert_equals(bs:select(varbinary.new('\x04'), 'BITS_ANY_SET'), {
            {'\xDE\xAD\xBE\xAF'},
            {'\xFE\xED\xFA\xCE'},
            {'\xFF'},
        })
        t.assert_equals(bs:select(varbinary.new('\x04'), 'BITS_ALL_NOT_SET'), {
            {'\xFA\xDE\xDE\xAD'},
        })
    end)
end

g.after_test('test_bitset_index', function(cg)
    cg.server:exec(function()
        if box.space.withdata then
            box.space.withdata:drop()
        end
    end)
end)
