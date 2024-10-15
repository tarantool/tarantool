local ffi = require('ffi')

-- This script can be used only with Tarantool without the fix for gh-9965. For
-- example, on 3.3.1.
box.cfg{}

local s = box.schema.space.create('test', {engine = 'vinyl'})
s:create_index('idx_str',  {parts = {1, 'string'}})
s:create_index('idx_uint', {parts = {2, 'unsigned'}, unique = false})
s:create_index('idx_int',  {parts = {3, 'integer'}, unique = false})
s:create_index('idx_dbl',  {parts = {4, 'double'}, unique = false})
s:create_index('idx_num',  {parts = {5, 'number'}, unique = false})

s:replace{'bigint1', 99999999999998, -99999999999998, 0, 0}
s:replace{'double_int',           0, 0, 10, 0}
s:replace{'double_int_as_double', 0, 0, ffi.cast('double', 11), 0}
s:replace{'double_int_as_float',  0, 0, ffi.cast('float', 12), 0}
s:replace{'double_double',        0, 0, 13.5, 0}
s:replace{'double_float',         0, 0, ffi.cast('float', 14.5), 0}
box.snapshot()

s:replace{'bigint2', 99999999999999, -99999999999999, 0, 0}
s:replace{'number_int',           0, 0, 0, 10}
s:replace{'number_int_as_double', 0, 0, 0, ffi.cast('double', 11)}
s:replace{'number_int_as_float',  0, 0, 0, ffi.cast('float', 12)}
s:replace{'number_double',        0, 0, 0, 13.5}
s:replace{'number_float',         0, 0, 0, ffi.cast('float', 14.5)}
box.snapshot()

os.exit(0)
