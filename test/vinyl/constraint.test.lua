
-- key type validations (str, num)
space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
space:insert{1}
space:replace{1}
space:delete{1}
space:update({1}, {{'=', 1, 101}})
space:upsert({1}, {{'+', 1, 10}})
space:get{1}
index:pairs(1, {iterator = 'GE'})
space:drop()

-- key type validations (num, str)
space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
space:insert{'A'}
space:replace{'A'}
space:delete{'A'}
space:update({'A'}, {{'=', 1, 101}})
space:upsert({'A'}, {{'+', 1, 10}})
space:get{'A'}
index:pairs('A', {iterator = 'GE'})
space:drop()

-- ensure all key-parts are passed
space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', { type = 'tree', parts = {1,'unsigned',2,'unsigned'} })
space:insert{1}
space:replace{1}
space:delete{1}
space:update(1, {{'=', 1, 101}})
space:upsert({1}, {{'+', 1, 10}})
space:get{1}
index:select({1}, {iterator = box.index.GT})
space:drop()

-------------------------------------------------------------------------------
-- Key part length without limit
-------------------------------------------------------------------------------

space = box.schema.space.create('single_part', { engine = 'vinyl' })
_ = space:create_index('primary', { type = 'tree', parts = {1, 'string'}})

t1 = space:insert({string.rep('x', 1020)})
t1 = space:insert({string.rep('x', 10210)})
t3 = space:insert({string.rep('x', 102200)})

space:drop()
space = nil
pk = nil
