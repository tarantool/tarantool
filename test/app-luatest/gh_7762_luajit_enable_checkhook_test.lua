local checktrace = require('jit.util').traceinfo
local t = require('luatest')

local g = t.group()
g.before_each(function()
    t.skip_if(not jit.status(), 'Test requires JIT enabled')
    t.skip_if(jit.os == 'BSD', 'Disabled on *BSD due to #4819')
end)

-- Test payload to be recorded. It calculates the arithmetic sum
-- of elements from 1 to the given n.
-- XXX: To avoid another fragile test, use loadstring to generate
-- the function with code motion agnostic line numbers to be used
-- in hook validation below.
local sumN = assert(load([[
    return function(n)
        local result = 0
        for i = 1, n do
            result = result + i
        end
        return result
    end
]]))()

-- Table with lines to be hit by debug hook below.
local lines = {
    2,  -- local result = 0
    3,  -- for i = 1, n do
    4,  --   result = result + i -- (i is 1)
    3,  -- for i = 1, n do
    4,  --   result = result + i -- (i is 2)
    3,  -- for i = 1, n do
    4,  --   result = result + i -- (i is 3)
    3,  -- for i = 1, n do
    6,  -- return result
}

g.test_luajit_enable_checkhook = function()
    -- Setup JIT engine to record a trace as soon as possible.
    jit.opt.start('hotloop=1')
    -- Purge all existing traces.
    jit.flush()

    -- Check there is no traces at the beginning of the test.
    assert(not checktrace(1), "JIT flush doesn't work")
    -- Run payload with the minimal possible number to force
    -- the inner loop to be compiled.
    sumN(3)
    -- Check the new trace is compiled as a result of the payload
    -- call above.
    assert(checktrace(1), 'The loop in <sumN> is compiled')

    -- Return JIT engine setup to its defaults.
    jit.opt.start('hotloop=56')

    -- Upvalue for a key from lines to be used in the scope of the
    -- the debug hook below.
    local lidx = 0

    -- XXX: After the hook is set, it is fired two more times in
    -- addition to all entries in <lines> table above:
    -- 1. The call of the compiled test payload below:
    --         local sum = sumN(3)
    --   In this case the hook is fired before any of the lines
    --   within <sumN> function, so we can ignore it by setting
    --   <lidx> initial value to zero. As a result the lookup
    --   in <lines> table yields nil for this case.
    -- 2. Turning of the hook after the test payload returns:
    --         debug.sethook()
    --   This is the last time the hook is fired, and <lidx> value
    --   equals to #lines + 1. As a result the lookup also yields
    --   nil for this case.
    -- Since the test is aimed to check whether the hook is fired
    -- on trace compiled for the payload above, there is no need
    -- to check the actual value of the line for the both
    -- aforementioned cases and we can simply skip the assertion.
    debug.sethook(function(hookname, line)
        -- XXX: Only line hooks are fired.
        assert(hookname == 'line', 'Unexpected hook type is fired')
        local lval = lines[lidx]
        if lval then
            t.assert_equals(line, lval)
        end
        lidx = lidx + 1
    end, 'l')

    -- Check the debug hook is fired while trace execution.
    local sum = sumN(3)

    -- Remove debug hook.
    debug.sethook()

    -- Check all lines have been hit by the debug hook above.
    t.assert_lt(#lines, lidx)
    -- Check the result of the payload call.
    t.assert_equals(sum, 6)
end
