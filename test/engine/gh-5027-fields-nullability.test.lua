test_run = require('test_run').new()

s = box.schema.space.create('gh-5027', {engine=test_run:get_cfg('engine')})
_ = s:create_index('i1', {parts={{1, 'unsigned'}}})
_ = s:create_index('i2', {parts={{5, 'unsigned', is_nullable=true}}})
s:replace{1}
s:replace{1, box.NULL}
s:replace{1, box.NULL, box.NULL}
s:replace{1, box.NULL, box.NULL, box.NULL}
s:drop()

s = box.schema.space.create('gh-5027', {engine=test_run:get_cfg('engine')})
_ = s:create_index('i1', {parts={{1, 'unsigned'}}})
_ = s:create_index('i2', {parts={{5, 'unsigned', is_nullable=false}}})
s:replace{1} -- error
s:replace{1, box.NULL} -- error
s:replace{1, box.NULL, box.NULL} -- error
s:replace{1, box.NULL, box.NULL, box.NULL} -- error
s:replace{1, box.NULL, box.NULL, box.NULL, 5} -- ok
s:drop()
