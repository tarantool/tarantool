test_run = require('test_run').new()

-- User cannot create spaces with this engine.
s = box.schema.space.create('test', {engine = 'service'})
