env = require('test_run')
test_run = env.new()

--
-- gh-4201: Introduce varbinary field type.
--
s = box.schema.space.create('withdata')
s:format({{"b", "integer"}})
_ = s:create_index('pk', {parts = {1, "varbinary"}})
s:format({{"b", "varbinary"}})
_ = s:create_index('pk', {parts = {1, "integer"}})
pk = s:create_index('pk', {parts = {1, "varbinary"}})

buffer = require('buffer')
ffi = require('ffi')

test_run:cmd("setopt delimiter ';'")
function encode_bin(bytes)
    local tmpbuf = buffer.ibuf()
    local p = tmpbuf:alloc(3 + #bytes)
    p[0] = 0x91
    p[1] = 0xC4
    p[2] = #bytes
    for i, c in pairs(bytes) do
        p[i + 3 - 1] = c
    end
    return tmpbuf
end
test_run:cmd("setopt delimiter ''");

test_run:cmd("setopt delimiter ';'")
function bintuple_insert(space, bytes)
    local tmpbuf = encode_bin(bytes)
    ffi.cdef[[int box_insert(uint32_t space_id, const char *tuple, const char *tuple_end, box_tuple_t **result);]]
    ffi.C.box_insert(space.id, tmpbuf.rpos, tmpbuf.wpos, nil)
end
test_run:cmd("setopt delimiter ''");

bintuple_insert(s, {0xDE, 0xAD, 0xBE, 0xAF})
bintuple_insert(s, {0xFE, 0xED, 0xFA, 0xCE})
s:select()
box.execute("SELECT * FROM \"withdata\" WHERE \"b\" < x'FEEDFACE';")
pk:alter({parts = {1, "scalar"}})
s:format({{"b", "scalar"}})
s:insert({11})
s:insert({22})
s:insert({"11"})
s:insert({"22"})
s:select()
box.execute("SELECT * FROM \"withdata\" WHERE \"b\" <= x'DEADBEAF';")
pk:alter({parts = {1, "varbinary"}})
s:delete({11})
s:delete({22})
s:delete({"11"})
s:delete({"22"})
bintuple_insert(s, {0xFA, 0xDE, 0xDE, 0xAD})
pk:alter({parts = {1, "varbinary"}})
s:select()

--
-- gh-5071: bitset index for binary fields
--
bs = s:create_index('bitset', {type = 'bitset', parts = {1, "varbinary"}})

bintuple_insert(s, {0xFF})

ITER_BITS_ALL_SET = 7
ITER_BITS_ANY_SET = 8
ITER_BITS_ALL_NOT_SET = 9

test_run:cmd("setopt delimiter ';'")
function varbinary_select(space, idx, bytes, flag)
    local tmpbuf = encode_bin(bytes)
    ffi.cdef[[
    box_iterator_t *box_index_iterator(uint32_t space_id, uint32_t index_id, int type,
           const char *key, const char *key_end);//
    int box_iterator_next(box_iterator_t *iterator, box_tuple_t **result);//
    const char *box_tuple_field(box_tuple_t *tuple, uint32_t fieldno);//
    ]]
    local res = ffi.new("box_tuple_t*[1]")
    local it = ffi.C.box_index_iterator(space.id, idx.id, flag, tmpbuf.rpos, tmpbuf.wpos)

    ffi.C.box_iterator_next(it, res)

    local output_table = {}

    while res[0] ~= nil do
        local field = ffi.C.box_tuple_field(res[0], 0)
        assert(bit.band(field[0], 0xff) == 0xc4)
        local len = field[1]
        assert(len >= 0)

        local output = ''
        for i = 0, len - 1 do
            output = output .. string.format("%x", bit.band(field[i+2], 0xff))
        end
        table.insert(output_table, output)

        ffi.C.box_iterator_next(it, res)
    end

    return output_table
end
test_run:cmd("setopt delimiter ''");

varbinary_select(s, bs, { 0xff }, ITER_BITS_ALL_SET)
varbinary_select(s, bs, { 0x04 }, ITER_BITS_ANY_SET)
varbinary_select(s, bs, { 0x04 }, ITER_BITS_ALL_NOT_SET)

s:drop()
