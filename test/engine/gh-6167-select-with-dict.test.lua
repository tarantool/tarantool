-- https://github.com/tarantool/tarantool/issues/6167
inspector = require('test_run').new()
engine = inspector:get_cfg('engine')

s = box.schema.create_space('test', {engine = engine})
_ = s:create_index('pk')
s:replace{1}
s:select{key = 'value'}
s:select{[2] = 1}
s:select{1, key = 'value'}
s:pairs{key = 'value'}
s:pairs{[2] = 1}
s:pairs{1, key = 'value'}
s:fselect{key = 'value'}
s:fselect{[2] = 1}
s:fselect{1, key = 'value'}
s:get{key = 'value'}
s:get{[2] = 1}
s:get{1, key = 'value'}
s:drop()
