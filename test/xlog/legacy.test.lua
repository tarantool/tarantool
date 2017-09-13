test_run = require('test_run').new()

version = test_run:get_cfg('version')
-- Use 1.7.5 snapshot to check that space formats are not checked.
-- It allows to use >= 1.6.5 format versions.
test_run:cmd('create server legacy with script="xlog/upgrade.lua", workdir="xlog/upgrade/1.7.5"')
test_run:cmd("start server legacy")

test_run:switch('legacy')

box.space._schema:get({'version'})
_space = box.space._space

--
-- Check _space 1.7.5 format.
--
_space:replace{600, 1, 'test', 'memtx', 0}
box.space.test:drop()

--
-- Check _index 1.6.5 format.
--
s = box.schema.space.create('s')
pk = s:create_index('pk')
sk = box.space._index:insert{s.id, 2, 'sk', 'rtree', 0, 1, 2, 'array'}
s.index.sk.parts
s.index.sk:drop()
box.space._index:insert{s.id, 2, 's', 'rtree', 0, 1, 2, 'thing'}
box.space._index:insert{s.id, 2, 's', 'rtree', 0, 1, 2, 'array', 'wtf'}
box.space._index:insert{s.id, 2, 's', 'rtree', 0, 0}
s:drop()

--
-- Check 1.6.5 space flags.
--
s = box.schema.space.create('t', { temporary = true })
index = s:create_index('primary', { type = 'hash' })
s:insert{1, 2, 3}
_ = _space:update(s.id, {{'=', 6, 'temporary'}})
s.temporary
_ = _space:update(s.id, {{'=', 6, ''}})
s.temporary
s:truncate()

_ = _space:update(s.id, {{'=', 6, 'no-temporary'}})
s.temporary
_ = _space:update(s.id, {{'=', 6, ',:asfda:temporary'}})
s.temporary
_ = _space:update(s.id, {{'=', 6, 'a,b,c,d,e'}})
s.temporary
_ = _space:update(s.id, {{'=', 6, 'temporary'}})
s.temporary

s:get{1}
s:insert{1, 2, 3}

_ = _space:update(s.id, {{'=', 6, 'temporary'}})
s.temporary
_ = _space:update(s.id, {{'=', 6, 'no-temporary'}})
s.temporary

s:delete{1}
_ = _space:update(s.id, {{'=', 6, 'no-temporary'}})

s:drop()

test_run:switch('default')
test_run:cmd('stop server legacy')
