test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- iterator (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
for key = 1, 100 do space:replace({tostring(key)}) end
t = {} for state, v in index:pairs({}, {iterator = 'ALL'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(tostring(44), {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(tostring(44), {iterator = 'GT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(tostring(77), {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(tostring(77), {iterator = 'LT'}) do table.insert(t, v) end
t
space:drop()


-- iterator (num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
for key = 1, 100 do space:replace({key}) end
t = {} for state, v in index:pairs({}, {iterator = 'ALL'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(44, {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(44, {iterator = 'GT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(77, {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(77, {iterator = 'LT'}) do table.insert(t, v) end
t
space:drop()


-- iterator multi-part (num, num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'num'} })
for key = 1, 100 do space:replace({key, key}) end
t = {} for state, v in index:pairs({}, {iterator = 'ALL'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({44, 44}, {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({44, 44}, {iterator = 'GT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({77, 77}, {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({77, 77}, {iterator = 'LT'}) do table.insert(t, v) end
t
space:drop()
