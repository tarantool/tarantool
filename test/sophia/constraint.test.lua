
-- key type validations (str, num)
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
space:insert{1}
space:replace{1}
space:delete{1}
space:update(1, {{'=', 1, 101}})
space:upsert(1, {{'+', 1, 10}}, {0})
space:get{1}
index:pairs(1, {iterator = 'GE'})
space:drop()

-- key type validations (num, str)
space = box.schema.space.create('test', { engine = 'sophia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
space:insert{'A'}
space:replace{'A'}
space:delete{'A'}
space:update('A', {{'=', 1, 101}})
space:upsert('A', {{'+', 1, 10}}, {0})
space:get{'A'}
index:pairs('A', {iterator = 'GE'})
space:drop()
