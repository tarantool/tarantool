test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- space create/drop
space = box.schema.space.create('test', { engine = engine })
space:drop()


-- space index create/drop
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
space:drop()


-- space index create/drop alter
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
_index = box.space[box.schema.INDEX_ID]
_index:delete{102, 0}
space:drop()


-- space index create/drop tree string
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', {type = 'tree', parts = {1, 'STR'}})
_ = space:insert({'test'})
space:drop()


-- space index create/drop tree num
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', {type = 'tree', parts = {1, 'num'}})
_ = space:insert({13})
space:drop()


-- space index create/drop tree multi-part num
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', {type = 'tree', parts = {1, 'num', 2, 'num'}})
_ = space:insert({13})
space:drop()


-- space index size
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
primary = space.index[0]
primary:len()
_ = space:insert({13})
_ = space:insert({14})
_ = space:insert({15})
primary:len()
space:drop()

-- Key part max
parts = {}
for i=1,box.schema.INDEX_PART_MAX,1 do parts[2 * i - 1] = i; parts[2 * i] = 'NUM' end
space = box.schema.space.create('test', { engine = engine })
_ = space:create_index('primary', { type = 'tree', parts = parts })

tuple = {}
for i=1,box.schema.INDEX_PART_MAX,1 do tuple[i] = i; end
_ = space:replace(tuple)
space:upsert(tuple, {{'=', box.schema.INDEX_PART_MAX + 1, 100500}})
space:get(tuple)
space:select(tuple)
_ = space:delete(tuple)

space:drop()

-- Too many key parts
parts = {}
for i=1,box.schema.INDEX_PART_MAX + 1,1 do parts[2 * i - 1] = i; parts[2 * i] = 'NUM' end
space = box.schema.space.create('test', { engine = engine })
_ = space:create_index('primary', { type = 'tree', parts = parts })
space:drop()
