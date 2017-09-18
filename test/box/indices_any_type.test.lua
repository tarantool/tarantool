-- Tests for HASH index type

s3 = box.schema.space.create('my_space4')
i3_1 = s3:create_index('my_space4_idx1', {type='HASH', parts={1, 'scalar', 2, 'integer', 3, 'number'}, unique=true})
i3_2 = s3:create_index('my_space4_idx2', {type='HASH', parts={4, 'string', 5, 'scalar'}, unique=true})
i3_3 = s3:create_index('my_space4_idx3', {type='TREE', parts={6, 'boolean'}, unique=false})
s3:insert({100.5, 30, 95, "str1", 5, true})
s3:insert({"abc#$23", 1000, -21.542, "namesurname", 99, false})
s3:insert({true, -459, 4000, "foobar", "36.6", true})
s3:select{}

i3_1:select({100.5})
i3_1:select({true, -459})
i3_1:select({"abc#$23", 1000, -21.542})

i3_2:select({"str1", 5})
i3_2:select({"str"})
i3_2:select({"str", 5})
i3_2:select({"foobar", "36.6"})

i3_3:select{true}
i3_3:select{false}
i3_3:select{}

s3:drop()

-- #2112 int vs. double compare
s5 = box.schema.space.create('my_space5')
_ = s5:create_index('primary', {parts={1, 'scalar'}})
-- small range 1
s5:insert({5})
s5:insert({5.1})
s5:select()
s5:truncate()
-- small range 2
s5:insert({5.1})
s5:insert({5})
s5:select()
s5:truncate()
-- small range 3
s5:insert({-5})
s5:insert({-5.1})
s5:select()
s5:truncate()
-- small range 4
s5:insert({-5.1})
s5:insert({-5})
s5:select()
s5:truncate()
-- conversion to another type is lossy for both values
s5:insert({18446744073709551615ULL})
s5:insert({3.6893488147419103e+19})
s5:select()
s5:truncate()
-- insert in a different order to excersise another codepath
s5:insert({3.6893488147419103e+19})
s5:insert({18446744073709551615ULL})
s5:select()
s5:truncate()
-- MP_INT vs MP_UINT
s5:insert({-9223372036854775808LL})
s5:insert({-3.6893488147419103e+19})
s5:select()
s5:truncate()
-- insert in a different order to excersise another codepath
s5:insert({-3.6893488147419103e+19})
s5:insert({-9223372036854775808LL})
s5:select()
s5:truncate()
-- different signs 1
s5:insert({9223372036854775807LL})
s5:insert({-3.6893488147419103e+19})
s5:select()
s5:truncate()
-- different signs 2
s5:insert({-3.6893488147419103e+19})
s5:insert({9223372036854775807LL})
s5:select()
s5:truncate()
-- different signs 3
s5:insert({-9223372036854775808LL})
s5:insert({3.6893488147419103e+19})
s5:select()
s5:truncate()
-- different signs 4
s5:insert({3.6893488147419103e+19})
s5:insert({-9223372036854775808LL})
s5:select()
s5:truncate()
-- different magnitude 1
s5:insert({1.1})
s5:insert({18446744073709551615ULL})
s5:select()
s5:truncate()
-- different magnitude 2
s5:insert({18446744073709551615ULL})
s5:insert({1.1})
s5:select()
s5:truncate()
-- Close values
ffi = require('ffi')
ffi.new('double', 1152921504606846976) == 1152921504606846976ULL
ffi.new('double', 1152921504606846977) == 1152921504606846976ULL
-- Close values 1
s5:insert({1152921504606846976ULL})
s5:insert({ffi.new('double', 1152921504606846976ULL)}) -- fail
s5:select()
s5:truncate()
-- Close values 2
s5:insert({1152921504606846977ULL})
s5:insert({ffi.new('double', 1152921504606846976ULL)}) -- success
s5:select()
s5:truncate()
-- Close values 3
s5:insert({-1152921504606846976LL})
s5:insert({ffi.new('double', -1152921504606846976LL)}) -- fail
s5:select()
s5:truncate()
-- Close values 4
s5:insert({-1152921504606846977LL})
s5:insert({ffi.new('double', -1152921504606846976LL)}) -- success
s5:select()
s5:truncate()
-- Close values 5
ffi.cdef "double exp2(double);"
s5:insert({0xFFFFFFFFFFFFFFFFULL})
s5:insert({ffi.new('double', ffi.C.exp2(64))}) -- success
s5:select()
s5:truncate()
-- Close values 6
s5:insert({0x8000000000000000LL})
s5:insert({ffi.new('double', -ffi.C.exp2(63))}) -- duplicate
s5:select()
s5:truncate()
-- Close values 7
s5:insert({0x7FFFFFFFFFFFFFFFLL})
s5:insert({ffi.new('double', ffi.C.exp2(63))}) -- ok
s5:select()
s5:truncate()

s5:drop()
