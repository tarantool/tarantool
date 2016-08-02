test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- https://github.com/tarantool/tarantool/issues/1109
-- Update via a secondary key breaks recovery
s = box.schema.create_space('test', { engine = engine })
i1 = s:create_index('test1', {parts = {1, 'num'}})
i2 = s:create_index('test2', {parts = {2, 'num'}})
s:insert{1, 2, 3}
s:insert{5, 8, 13}
i2:update({2}, {{'+', 3, 3}})
tmp = i2:delete{8}
inspector:cmd("restart server default")
box.space.test:select{}
box.space.test:drop()
