test_run = require('test_run').new()
trigger = function() end

s = box.schema.space.create('gh-5093', {engine=test_run:get_cfg('engine')})
_ = s:create_index('value', {parts={{1, type='decimal'}}})
_ = s:before_replace(trigger)
s:delete('1111.1111')
s:drop()
