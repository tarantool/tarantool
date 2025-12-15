local tap = require('tap')

local test = tap.test("or-232-unsink-64-kptr")
test:plan(1)

--- From: Thibault Charbonnier <thibaultcha@me.com>
--- tests: ffi: added a test case unsinking a 64-bit pointer from
--- a constant.
---
--- This test case reproduces the issue observed at:
--- https://github.com/openresty/lua-resty-core/issues/232 and was
--- contributed by @lukego and myself.
---
--- Co-authored-by: Luke Gorrie <lukego@gmail.com>
---
local ffi = require("ffi")

local array = ffi.new("struct { int x; } [1]")

-- This test forces the VM to unsink a pointer that was
-- constructed from a constant. The IR will include a 'cnewi'
-- instruction to allocate an FFI pointer object, the pointer
-- value will be an IR constant, the allocation will be sunk, and
-- the allocation will at some point be "unsunk" due to a
-- reference in the snapshot for a taken exit.

-- Note: JIT will recognize <array> as a "singleton" and allow its
-- address to be inlined ("constified") instead of looking up the
-- upvalue at runtime.

local function fn(i)
    -- Load pointer that the JIT will constify.
    local struct = array[0]
    -- Force trace exit when i==1000.
    -- luacheck: ignore
    if i == 1000 then end
    -- Ensure that 'struct' is live after exit.
    struct.x = 0
end

-- Loop over the function to make it compile and take a trace exit
-- during the final iteration.
for i = 1, 1000 do
    fn(i)
end
test:ok(true, "allocation is unsunk at the trace exit (no platform failures)")

test:done(true)
