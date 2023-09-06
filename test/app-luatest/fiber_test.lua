local fiber = require('fiber')
local t = require('luatest')
local g = t.group('fiber')

-- Test __serialize metamethod of the fiber.
g.test_serialize = function()
    local f = fiber.new(function() end)
    local fid = f:id()
    f:name('test fiber')

    -- Serialize a ready fiber.
    t.assert_equals(f:__serialize(),
                    { id = fid, name = 'test fiber', status = 'suspended' })

    -- gh-4265: Serializing a finished fiber should not raise an error.
    fiber.yield()
    t.assert_equals(f:__serialize(), { id = fid, status = 'dead' })

    -- Serialize a running fiber.
    t.assert_equals(fiber.self():__serialize(),
                    { id = fiber.self():id(),
                      name = 'luatest',
                      status = 'running' })
end

-- Test __tostring metamethod of the fiber.
g.test_tostring = function()
    local f = fiber.new(function() end)
    local fid = f:id()

    t.assert_equals(tostring(f), "fiber: " .. fid)

    -- gh-4265: Printing a finished fiber should not raise an error.
    fiber.yield()
    t.assert_equals(tostring(f), "fiber: " .. fid .. " (dead)")
end
