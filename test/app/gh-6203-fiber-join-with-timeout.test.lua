fiber = require('fiber')

timeout = 10
function sleep(timeout) \
    channel:get() \
    fiber.sleep(timeout) \
end

channel = fiber.channel()
f = fiber.create(sleep, timeout)
f:set_joinable(true)
channel:put(true)
f:join(0.1) -- Join failed because of timeout

timeout = 0.2
f = fiber.create(sleep, timeout)
f:set_joinable(true)
channel:put(true)
f:join("xxx") --error: 'fiber:join(timeout): bad arguments'
f:join(-1) --error: 'fiber:join(timeout): bad arguments'

