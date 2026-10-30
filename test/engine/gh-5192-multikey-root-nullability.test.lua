test_run = require('test_run').new()

s = box.schema.space.create('gh-5027', {engine=test_run:get_cfg('engine')})
_ = s:format({{name='id'}, {name='data', type='array', is_nullable=true}})
_ = s:create_index('i1', {parts={{1, 'unsigned'}}})
s:replace{1, box.NULL} -- ok
_ = s:create_index('i2', {parts={{field=2, path='[*].key', type='string'}}})
s:replace{2, box.NULL} -- ok
_ = s:delete(2)
s:drop()

s = box.schema.space.create('gh-5027', {engine=test_run:get_cfg('engine')})
_ = s:format({{name='id'}, {name='data', type='array'}})
_ = s:create_index('i1', {parts={{1, 'unsigned'}}})
s:replace{1, box.NULL} -- error
_ = s:create_index('i2', {parts={{field=2, path='[*].key', type='string'}}})
s:replace{2, box.NULL} -- error
s:replace{3, {}} -- ok
s:drop()
