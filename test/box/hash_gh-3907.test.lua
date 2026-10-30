-- gh-3907: check that integer numbers stored as MP_FLOAT/MP_DOUBLE
-- are hashed as MP_INT/MP_UINT.

ffi = require('ffi')
s = box.schema.space.create('test')
_ = s:create_index('primary', {type = 'hash', parts = {1, 'number'}})
s:insert{ffi.new('double', 0)}
s:insert{ffi.new('double', -1)}
s:insert{ffi.new('double', 9007199254740992)}
s:insert{ffi.new('double', -9007199254740994)}
s:get(0LL)
s:get(-1LL)
s:get(9007199254740992LL)
s:get(-9007199254740994LL)
s:drop()
