#!/usr/bin/env tarantool

-- The issue (gh-5388) is about a leak of the tuple, serialized
-- from a given key in the key_def:compare_with_key(tuple, key)
-- method.
--
-- It is tricky to verify this particular problem (we don't have a
-- counter of runtime tuples or runtime arena statistics).
--
-- The test is not about this problem. Aside of the tuple leak,
-- there was another problem in the fixed code: fiber's region
-- memory that is used for serialization of the key is 'leaked'
-- as well.
--
-- The test verifies that the fiber region does not hold any extra
-- memory after failed serialization of the key.

local ffi = require('ffi')
local key_def_lib = require('key_def')
local fiber = require('fiber')
local tap = require('tap')

ffi.cdef([[
    void
    box_region_truncate(size_t size);
]])

local function fiber_region_memory_used()
    return fiber.info()[fiber.self().id()].memory.used
end

local test = tap.test('gh-5388-lua-key_def-leak')
test:plan(1)

local key_def = key_def_lib.new({
    {type = 'string', fieldno = 1},
})
local tuple = box.tuple.new({'foo', 'bar'})

-- Choice of data size for the key below is a bit tricky.
--
-- mpstream does not register every allocation in the region
-- statistics as 'used', but it does after draining a slab (see
-- mpstream_reserve_slow(): region_alloc() counts 'used',
-- region_reserve() does not).
--
-- We should ensure that our 'large' key will be larger than a
-- first slab that the fiber region will give to the mpstream (to
-- exhaust it and see the 'leak' in the 'used' statistics). But
-- we have no easy way to do it.
--
-- Well at this point current slab of region fiber should be as after
-- the fiber initialization. Hopefully we won't have this
-- slab very large because we need to deal with thousands of fibers. So
-- I guess this slab (and it's unused area) is smaller than 1M.
--
-- Unfortunatly if/when the above assumtion becomes false the test will stop
-- testing the leak and will silently pass.
--
-- Serialization of this key should fail because function is not serializable.
local large_data = string.rep('x', 1048576)
local key = {large_data, function() end}

-- Verify that data allocated on the fiber region (&fiber()->gc)
-- during serialization of the key is freed when the serialization
-- fails.
local before = fiber_region_memory_used()
pcall(key_def.compare_with_key, key_def, tuple, key)
local after = fiber_region_memory_used()
test:is(after - before, 0, 'fiber region does not leak')

os.exit(test:check() and 0 or 1)
