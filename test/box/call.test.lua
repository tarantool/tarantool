box.schema.user.grant('guest', 'execute', 'universe')

conn = require('net.box').connect(box.cfg.listen)
conn:ping()

--
-- gh-291: IPROTO: call returns wrong tuple
--

function return_none() return end
conn:call("return_none")
conn:eval("return return_none()")
conn:call_16("return_none")

function return_nil() return nil end
conn:call("return_nil")
conn:eval("return return_nil()")
conn:call_16("return_nil")

function return_nils() return nil, nil, nil end
conn:call("return_nils")
conn:eval("return return_nils()")
conn:call_16("return_nils")

function return_bool() return true end
conn:call("return_bool")
conn:eval("return return_bool()")
conn:call_16("return_bool")

function return_bools() return true, false, true end
conn:call("return_bools")
conn:eval("return return_bools()")
conn:call_16("return_bools")

function return_number() return 1 end
conn:call("return_number")
conn:eval("return return_number()")
conn:call_16("return_number")

function return_numbers() return 1, 2, 3 end
conn:call("return_numbers")
conn:eval("return return_numbers()")
conn:call_16("return_numbers")

function return_string() return "string" end
conn:call("return_string")
conn:eval("return return_string()")
conn:call_16("return_string")

function return_strings() return "a", "b", "c" end
conn:call("return_strings")
conn:eval("return return_strings()")
conn:call_16("return_strings")

function return_emptytuple() return box.tuple.new() end
conn:call("return_emptytuple")
conn:eval("return return_emptytuple()")
conn:call_16("return_emptytuple")

function return_tuple() return box.tuple.new(1, 2, 3) end
conn:call("return_tuple")
conn:eval("return return_tuple()")
conn:call_16("return_tuple")

function return_tuples() return box.tuple.new(1, 2, 3), box.tuple.new(3, 4, 5)  end
conn:call("return_tuples")
conn:eval("return return_tuples()")
conn:call_16("return_tuples")

function return_map() return { k1 = 'v1', k2 = 'v2'} end
conn:call("return_map")
conn:eval("return return_map()")
conn:call_16("return_map")

function return_emptyarray() return {} end
conn:call("return_emptyarray")
conn:eval("return return_emptyarray()")
conn:call_16("return_emptyarray")

function return_array1() return {1} end
conn:call("return_array1")
conn:eval("return return_array1()")
conn:call_16("return_array1")

function return_array2() return {1, 2} end
conn:call("return_array2")
conn:eval("return return_array2()")
conn:call_16("return_array2")

function return_complexarray1() return {1, 2, {k1 = 'v1', k2 = 'v2' }} end
conn:call("return_complexarray1")
conn:eval("return return_complexarray1()")
conn:call_16("return_complexarray1")

function return_complexarray2() return {{k1 = 'v1', k2 = 'v2' }, 2, 3} end
conn:call("return_complexarray2")
conn:eval("return return_complexarray2()")
conn:call_16("return_complexarray2")

function return_complexarray3() return {1, {k1 = 'v1', k2 = 'v2' }, 3} end
conn:call("return_complexarray3")
conn:eval("return return_complexarray3()")
conn:call_16("return_complexarray3")

function return_complexarray4() return {{k1 = 'v1', k2 = 'v2' }} end
conn:call("return_complexarray4")
conn:eval("return return_complexarray4()")
conn:call_16("return_complexarray4")

function return_tableofarrays1() return {{1}} end
conn:call("return_tableofarrays1")
conn:eval("return return_tableofarrays1()")
conn:call_16("return_tableofarrays1")

function return_tableofarrays2() return {{1, 2, 3}} end
conn:call("return_tableofarrays2")
conn:eval("return return_tableofarrays2()")
conn:call_16("return_tableofarrays2")

function return_tableofarrays3() return {{1}, {2}, {3}} end
conn:call("return_tableofarrays3")
conn:eval("return return_tableofarrays3()")
conn:call_16("return_tableofarrays3")

function return_tableoftuples1() return {box.tuple.new(1)} end
conn:call("return_tableoftuples1")
conn:eval("return return_tableoftuples1()")
conn:call_16("return_tableoftuples1")

function return_tableoftuples2() return {box.tuple.new(1), box.tuple.new(2)} end
conn:call("return_tableoftuples2")
conn:eval("return return_tableoftuples2()")
conn:call_16("return_tableoftuples2")

function return_indecipherable1() return {{1}, 2, 3} end
conn:call("return_indecipherable1")
conn:eval("return return_indecipherable1()")
conn:call_16("return_indecipherable1")

function return_indecipherable2() return {box.tuple.new(1), 2, 3} end
conn:call("return_indecipherable2")
conn:eval("return return_indecipherable2()")
conn:call_16("return_indecipherable2")

function return_indecipherable3() return {1, {2}, 3} end
conn:call("return_indecipherable3")
conn:eval("return return_indecipherable3()")
conn:call_16("return_indecipherable3")

function return_indecipherable4() return {1, box.tuple.new(2), 3} end
conn:call("return_indecipherable4")
conn:eval("return return_indecipherable4()")
conn:call_16("return_indecipherable4")

function toarray(x) return setmetatable(x, { __serialize = 'array' }) end
function tomap(x) return setmetatable(x, { __serialize = 'map' }) end

function return_serialize1() return toarray({ [1] = 1, [20] = 1}) end
conn:call("return_serialize1")
conn:eval("return return_serialize1()")
conn:call_16("return_serialize1")

function return_serialize2() return tomap({ 'a', 'b', 'c'}) end
conn:call("return_serialize2")
conn:eval("return return_serialize2()")
conn:call_16("return_serialize2")

function return_serialize3() return {'x', toarray({ [1] = 1, [20] = 1})} end
conn:call("return_serialize3")
conn:eval("return return_serialize3()")
conn:call_16("return_serialize3")

function return_serialize4() return {'x', tomap({ 'a', 'b', 'c'})} end
conn:call_16("return_serialize4")

function return_serialize5() return {toarray({ [1] = 1, [20] = 1}), 'x'} end
conn:call("return_serialize5")
conn:eval("return return_serialize5()")
conn:call_16("return_serialize5")

function return_serialize6() return { tomap({ 'a', 'b', 'c'}), 'x'} end
conn:call("return_serialize6")
conn:eval("return return_serialize6()")
conn:call_16("return_serialize6")

function return_serialize7() return {toarray({ [1] = 1, [20] = 1})} end
conn:call("return_serialize7")
conn:eval("return return_serialize7()")
conn:call_16("return_serialize7")

function return_serialize8() return { tomap({ 'a', 'b', 'c'})} end
conn:call("return_serialize8")
conn:eval("return return_serialize8()")
conn:call_16("return_serialize8")

--
-- gh-1167
--
sparse_safe = require('msgpack').cfg.encode_sparse_safe
sparse_safe

function return_sparse1() local res = {} res[1] = 1 res[20] = 1 return res end
conn:call("return_sparse1")
conn:eval("return return_sparse1()")
conn:call_16("return_sparse1")

function return_sparse2() return { [1] = 1, [20] = 1} end
conn:call("return_sparse2")
conn:eval("return return_sparse2()")
conn:call_16("return_sparse2")

function return_sparse3() local res = {} res[5] = 5 res[20] = 1 return res end
conn:call("return_sparse3")
conn:eval("return return_sparse3()")
conn:call_16("return_sparse3")

function return_sparse4() return { [5] = 1, [20] = 1} end
conn:call("return_sparse4")
conn:eval("return return_sparse4()")
conn:call_16("return_sparse4")

require('msgpack').cfg { encode_sparse_safe = 50 }

conn:call("return_sparse1")
conn:eval("return return_sparse1()")
conn:call_16("return_sparse1")

conn:call("return_sparse2")
conn:eval("return return_sparse2()")
conn:call_16("return_sparse2")

conn:call("return_sparse3")
conn:eval("return return_sparse3()")
conn:call_16("return_sparse3")

conn:call("return_sparse4")
conn:eval("return return_sparse4()")
conn:call_16("return_sparse4")

conn:close()
require('msgpack').cfg { encode_sparse_safe = sparse_safe }

box.schema.user.revoke('guest', 'execute', 'universe')
