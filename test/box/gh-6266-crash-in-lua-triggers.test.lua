env = require('test_run')
test_run = env.new()

fiber = require('fiber')
channel = fiber.channel(1)
s = box.schema.space.create('test')
_ = s:create_index('primary')
_ = s:on_replace(function() fiber.sleep(1) channel:put(true) end)
_ = fiber.create(function() s:replace({7}) end)
-- destroy on_replace triggers
s:drop()
_ = channel:get()
