test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')
errinj = box.error.injection

-- truncation rollback should not crash
s = box.schema.space.create('truncate_rollback', {engine = engine})
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {1, 'int'}})
for i = 1, 10 do s:replace({i, i}) end
errinj.set('ERRINJ_WAL_IO', true)
s:truncate()
errinj.set('ERRINJ_WAL_IO', false)
s:select()
s:drop()
