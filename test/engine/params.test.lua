--init
test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

s = box.schema.create_space('engine', {engine=engine})
i = s:create_index('primary')

--test example for memtx and vinyl
box.space.engine:insert{1,2,3}
box.space.engine:select{}


-- cleanup
box.space.engine:drop()
