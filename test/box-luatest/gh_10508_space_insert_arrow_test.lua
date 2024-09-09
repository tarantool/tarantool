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

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

-- MP_EXT of type MP_ARROW, column 'a', value 0.
local mp_arrow_hex = [[
    c8011008ffffffff70000000040000009effffff0400010004000000b6ffffff0c0000000400
    0000000000000100000004000000daffffff140000000202000004000000f0ffffff40000000
    01000000610000000600080004000c0010000400080009000c000c000c000000040000000800
    0a000c00040006000800ffffffff88000000040000008affffff040003001000000008000000
    0000000000000000acffffff0100000000000000340000000800000000000000020000000000
    0000000000000000000000000000000000000000000008000000000000000000000001000000
    010000000000000000000000000000000a00140004000c0010000c0014000400060008000c00
    00000000000000000000
]]

g.test_space_insert_arrow = function(cg)
    cg.server:exec(function(mp_arrow_hex)
        local json = require('json')
        local msgpack = require('msgpack')
        local s = box.schema.create_space('test')

        t.assert_error_msg_equals(
            "Usage: space:insert_arrow(arrow_record_batch)",
            box.internal.insert_arrow)
        t.assert_error_msg_equals(
            "Usage: space:insert_arrow(arrow_record_batch)",
            box.internal.insert_arrow, 'not a number', 123)
        t.assert_error_msg_equals(
            "Use space:insert_arrow(...) instead of space.insert_arrow(...)",
            s.insert_arrow)
        t.assert_error_msg_equals(
            "Space 'xyz' does not exist",
            s.insert_arrow, {id = 1000, name = 'xyz'})
        t.assert_error_msg_content_equals(
            "bad argument #1 to 'insert_arrow' (arrow_record_batch expected," ..
            " got string)",
            function() s:insert_arrow('not an arrow record batch') end)
        t.assert_error_msg_content_equals(
            "bad argument #1 to 'insert_arrow' (arrow_record_batch expected," ..
            " got userdata)",
            function() s:insert_arrow(box.tuple.format.new({})) end)

        local mp_arrow = string.fromhex(mp_arrow_hex:gsub("%s+", ""))
        local arrow_object, next_pos = msgpack.decode(mp_arrow)
        t.assert_equals(next_pos, 277)

        -- Test `__tostring() method'.
        t.assert_equals(tostring(arrow_object), "arrow_record_batch")
        -- Test `__serialize() method'.
        t.assert_equals(json.encode(arrow_object), "\"arrow_record_batch\"")

        t.assert_error_msg_equals(
            "memtx does not support arrow format",
            s.insert_arrow, s, arrow_object)
    end, {mp_arrow_hex})
end
