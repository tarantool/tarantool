fiber = require('fiber')

-- fiber.cond
c = fiber.cond()
tostring(c)
-- args validation
c.wait()
c.wait('1')
c:wait('1')
c:wait(-1)
-- timeout
c:wait(0.1)
-- wait success
fiber.create(function() fiber.sleep(.5); c:broadcast() end) and c:wait(.6)
-- signal
t = {}
for i = 1,4 do fiber.create(function() c:wait(); table.insert(t, '#') end) end
c:signal()
fiber.sleep(0.1)
t
-- broadcast
c:broadcast()
fiber.sleep(0.1)
t
