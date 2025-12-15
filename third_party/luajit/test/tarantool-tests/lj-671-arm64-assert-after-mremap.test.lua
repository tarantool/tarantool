local tap = require('tap')

-- Test file to demonstrate assertion after `mremap()` on arm64.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/671.

local test = tap.test('lj-671-arm64-assert-after-mremap')
test:plan(1)

-- `mremap()` is used on Linux to remap directly mapped big
-- (>=DEFAULT_MMAP_THRESHOLD) memory chunks.
-- The simplest way to test memory move is to allocate the huge
-- memory chunk for string buffer directly and reallocate it
-- after.
-- To allocate a memory buffer with the size up to the threshold
-- for direct mapping `string.rep()` is used with the length that
-- equals to DEFAULT_MMAP_THRESHOLD.
-- Then concatenate the directly mapped result string with the
-- other one to trigger buffer reallocation and its remapping.

local DEFAULT_MMAP_THRESHOLD = 128 * 1024
local s = string.rep('x', DEFAULT_MMAP_THRESHOLD)..'x'
test:ok(s)

test:done(true)
