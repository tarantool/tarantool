
-- key type validations (str, num)
space = box.schema.space.create('test', { engine = 'phia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
space:insert{1}
space:replace{1}
space:delete{1}
space:update({1}, {{'=', 1, 101}})
space:upsert({1}, {{'+', 1, 10}})
space:get{1}
index:pairs(1, {iterator = 'GE'})
space:drop()

-- key type validations (num, str)
space = box.schema.space.create('test', { engine = 'phia' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
space:insert{'A'}
space:replace{'A'}
space:delete{'A'}
space:update({'A'}, {{'=', 1, 101}})
space:upsert({'A'}, {{'+', 1, 10}})
space:get{'A'}
index:pairs('A', {iterator = 'GE'})
space:drop()


-- test key-parts limit (8 max)
space = box.schema.space.create('test', { engine = 'phia' })
index = space:create_index('primary', { type = 'tree', parts = {1,'num',2,'num',3,'num',4,'num',5,'num',6,'num',7,'num',8,'num'} })
space:drop()

-- test key-parts limit (hit the limit)
space = box.schema.space.create('test', { engine = 'phia' })
index = space:create_index('primary', { type = 'tree', parts = {1,'num',2,'num',3,'num',4,'num',5,'num',6,'num',7,'num',8,'num', 9, 'num'} })
space:drop()


-- ensure all key-parts are passed
space = box.schema.space.create('test', { engine = 'phia' })
index = space:create_index('primary', { type = 'tree', parts = {1,'num',2,'num'} })
space:insert{1}
space:replace{1}
space:delete{1}
space:update(1, {{'=', 1, 101}})
space:upsert({1}, {{'+', 1, 10}})
space:get{1}
index:select({1}, {iterator = box.index.GT})
space:drop()
