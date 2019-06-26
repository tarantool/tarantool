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
function bintuple_insert(space, bytes)
	local tmpbuf = buffer.IBUF_SHARED
	tmpbuf:reset()
	local p = tmpbuf:alloc(3 + #bytes)
	p[0] = 0x91
	p[1] = 0xC4
	p[2] = #bytes
	for i, c in pairs(bytes) do p[i + 3 - 1] = c end
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
s:drop()
