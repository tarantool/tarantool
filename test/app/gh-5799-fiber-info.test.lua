test_run = require('test_run').new()
fiber = require('fiber')
json = require('json')

function yielder(n) for i=1, n do fiber.yield() end end

csw_check_counter = 0
fibers = {}

test_run:cmd('setopt delimiter ";"')
for i=1,100 do
    fibers[i] = fiber.new(function()
        for j=1,10 do
            fiber.yield()
            if j == fiber.self():csw() and j == fiber.self():info().csw then
                csw_check_counter = csw_check_counter + 1
            end
        end
    end)
    fibers[i]:set_joinable(true)
end;
for i=1,100 do
    fibers[i]:join()
end;
test_run:cmd('setopt delimiter ""');
csw_check_counter

cond = fiber.cond()
running = fiber.create(function() cond:wait() end)
json.encode(running:info()) == json.encode(fiber.info()[running:id()])
cond:signal()
dead = fiber.create(function() end)

dead:csw()
dead:info()
