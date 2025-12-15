local tap = require('tap')
local ffi = require('ffi')
local test = tap.test('lj-445-fix-memory-probing-allocator'):skipcond({
  ['Unlikely to hit beyond the upper bound for GC64'] = ffi.abi('gc64'),
})

local bit = require('bit')
local shr = bit.rshift
local uintptr_t = ffi.typeof('uintptr_t')

-- Due to limitations in the x64 compiler backend, max memory
-- limit is two times lower when JIT is not disabled entirely.
local HAS_JIT = jit.status()
local LJ_ALLOC_MBITS = HAS_JIT and 31 or 32
local MAX_GB = HAS_JIT and 2 or 4

test:plan(MAX_GB)

-- Chomp memory in currently allocated GC space.
collectgarbage('stop')

-- XXX: This test allocates `cdata` objects, but in real world
-- scenarios it can be any object that is allocated with
-- LuaJIT's allocator, including, for example, trace, if it
-- has been allocated close enough to the memory region
-- upper bound and if it is long enough.
--
-- When this issue occurs with a trace, it may lead to
-- failures in checks that rely on pointers being 32-bit.
-- For example, you can see one here: src/lj_asm_x86.h:370.
--
-- Although it is nice to have a reproducer that shows how
-- that issue can affect a non-synthetic execution, it is really
-- hard to achieve the described situation with traces because
-- allocations are hint-based and there is no robust enough
-- way to create a deterministic test for this behavior.

-- Every allocation must either result in a chunk that fits into
-- the `MAX_GB`-sized region entirely or return an OOM error.
for _ = 1, MAX_GB do
  local status, result = pcall(ffi.new, 'char[?]', 1024 * 1024 * 1024)
  if status then
    local upper_bound = ffi.cast(uintptr_t, result) + ffi.sizeof(result)
    test:ok(shr(upper_bound, LJ_ALLOC_MBITS) == 0, 'non-extended address')
  else
    test:ok(result == 'not enough memory', 'OOM encountered')
  end
end

test:done(true)
