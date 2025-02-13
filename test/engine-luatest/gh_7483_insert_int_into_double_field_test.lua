local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-7483-insert-int-into-double-field-test', {
    {engine = 'memtx', index_type = 'tree'},
    {engine = 'memtx', index_type = 'hash'},
    {engine = 'vinyl', index_type = 'tree'},
})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.s ~= nil then
            box.space.s:drop()
        end
    end)
end)

--
-- This test case had to be changed because of 9965. Double index intentionally
-- stores and sorts integers like integers.
--
g.test_insert_int_into_double_field = function(cg)
    cg.server:exec(function (engine, index_type)
        local ffi = require('ffi')

        local s = box.schema.space.create('s', { engine = engine })
        s:create_index('s_idx', {type = index_type, parts = {1, 'double'}})

        local int_1 = 1
        local double_1 = ffi.new('double', 1.0)
        local errdup = 'Duplicate key exists in unique index "s_idx" in space '
                       ..'"s" with old tuple - '
        s:insert({int_1})
        t.assert_error_msg_contains(errdup, s.insert, s, {double_1})
        t.assert_equals(s:get{double_1}, {int_1});

        -- 2 ** 63 - 1 can't be directly converted to double,
        -- it becomes 2 ** 63 on cast.
        local int_2e63_minus_1 = 0x7fffffffffffffffULL
        local double_2e63 = ffi.new('double', int_2e63_minus_1)

        s:insert({int_2e63_minus_1})
        s:insert({double_2e63})

        t.assert_equals(s:get{double_2e63}, {double_2e63})
        t.assert_equals(s:get{int_2e63_minus_1}, {int_2e63_minus_1})
    end, {cg.params.engine, cg.params.index_type})
end

g.test_alter = function(cg)
    cg.server:exec(function(engine, index_type)
        local s = box.schema.space.create('s', {engine = engine})
        local pk = s:create_index('s_idx', {
            type = index_type, parts = {1, 'double'}})
        s:replace{16}
        t.assert_equals(s:get{16}, {16})
        pk:alter({parts = {1, 'number'}})
        t.assert_equals(s:get{16}, {16})
    end)
end
