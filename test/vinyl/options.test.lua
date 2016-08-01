utils = require('utils')
test_run = require('test_run').new()

index_options = test_run:get_cfg('index_options')
index_options.type = 'TREE'
index_options.parts = {1, 'unsigned'}

space = box.schema.space.create('test', { engine = 'vinyl' })
_ = space:create_index('primary', index_options)
utils.check_space(space, 1024)
space:drop()

test_run = nil
utils = nil
