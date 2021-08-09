test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

space = box.schema.space.create('test', { engine = engine, format = { { 'id', 'unsigned' }, { 'double_field', 'double' } } })
index = space:create_index('primary', { type = 'tree', parts = { 1, 'unsigned' } })

space:insert { 1, 1.0 }
space:select { 1 }

space:drop {}
