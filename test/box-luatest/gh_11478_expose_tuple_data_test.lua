local t = require("luatest")

local g = t.group()

g.test_expose_tuple_data_api = function()
    local ffi = require('ffi')
    local msgpack = require('msgpack')
    local test_tuple_table = {true, 2, "test"}
    local test_tuple = box.tuple.new(test_tuple_table)
    ffi.cdef([[
        const char * box_tuple_data(box_tuple_t *tuple, size_t *bsize);

        size_t box_tuple_bsize(box_tuple_t *tuple);
    ]])
    local bsize_cdata_buf = ffi.new("size_t[1]")
    local data_ptr = ffi.C.box_tuple_data(test_tuple, bsize_cdata_buf)
    local bsize = tonumber(bsize_cdata_buf[0])
    local actual_bsize = ffi.C.box_tuple_bsize(test_tuple)
    t.assert_equals(bsize, actual_bsize)
    local msgpack_str = ffi.string(data_ptr, bsize)
    local decoded_table, _ = msgpack.decode(msgpack_str)
    t.assert_equals(decoded_table, test_tuple_table)
end
