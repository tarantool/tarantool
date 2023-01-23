local fiber = require('fiber')
local t = require('luatest')

local g1 = t.group('gh-7489-concurrent-fiber-join-1',
                   {{ do_cancel = false },
                    { do_cancel = true }})
local g2 = t.group('gh-7489-concurrent-fiber-join-2')

local error_message = 'the fiber is already joined by concurrent fiber:join()'

g1.test_concurrent_fiber_join = function(cg)
    local block1 = true
    -- 1. Create f1, but do not start it
    local f1 = fiber.new(function()
        -- 4. Wait for unblocking and exit
        while block1 do
            fiber.sleep(0.001)
        end
    end)
    f1:set_joinable(true)
    -- 2. Create f2, but do not start it
    local f2 = fiber.new(function()
        block1 = false
        -- 5. Try to join f1. Yield here until it is unblocked and finished
        f1:join()
    end)
    f2:set_joinable(true)
    -- 3. Start f1, then f2
    fiber.yield()
    -- 6. Optionally cancel f1
    if cg.params.do_cancel then
        f1:cancel()
    end
    -- 7. Try to join f1. Yield here until it is unblocked and finished.
    -- This join should raise an error, because f1 is already joined by f2
    t.assert_error_msg_content_equals(error_message, function() f1:join() end)
end

g2.test_concurrent_fiber_join = function()
    local f1 = nil
    local f2 = nil
    local function test_f1()
        -- Do nothing
    end
    local function test_f2()
        while true do
            fiber.sleep(0.001)
        end
    end
    local function joiner_f1()
       -- 3. Try to join f1. Yield until it is started and immediately finished
       local st = f1:join()
       -- 5. Create a new fiber, which sleeps until it is cancelled
       f2 = fiber.new(test_f2)
       f2:set_joinable(true)
       return st
    end
    local function joiner_f2()
       -- 4. Try to join f1. Yield until it is started and immediately finished
       local st, err = pcall(fiber.join, f1)
       -- 6. This join should raise an error, because f1 should be already
       -- joined by the first joiner.
       -- In case of a bug, it may hang trying to join f2 instead of f1
       return not st and err == error_message
    end
    -- 1. Create two joiners and one joinee fibers, but do not start them
    local j1 = fiber.new(joiner_f1)
    local j2 = fiber.new(joiner_f2)
    f1 = fiber.new(test_f1)
    j1:set_joinable(true)
    j2:set_joinable(true)
    f1:set_joinable(true)
    -- 2. Start fibers in the following order: j1, j2, f1
    fiber.yield()
    -- 7. Verify that the first join succeeded and the second one failed
    local _, joiner1_passed = j1:join()
    local _, joiner2_passed = j2:join()
    t.assert(joiner1_passed)
    t.assert(joiner2_passed)
    f2:cancel()
    f2:join()
end
