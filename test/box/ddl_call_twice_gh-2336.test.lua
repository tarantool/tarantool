-- gh-2336 crash if format called twice during snapshot
fiber = require'fiber'

space = box.schema.space.create('test_format')
_ = space:create_index('pk', { parts = { 1,'str' }})
space:format({{ name ="key"; type = "string" }, { name ="dataAB"; type = "string" }})
str = string.rep("t",1024)
for i = 1, 10000 do space:insert{tostring(i), str} end
ch = fiber.channel(3)
_ = fiber.create(function() fiber.yield() box.snapshot() ch:put(true) end)
format = {{name ="key"; type = "string"}, {name ="data"; type = "string"}}
for i = 1, 2 do fiber.create(function() fiber.yield() local ok, err = pcall(space.format, space, format) ch:put(ok or err) end) end

-- The second format update will instantly fail because the first one is being committed
{ch:get(), ch:get(), ch:get()}

space:drop()
