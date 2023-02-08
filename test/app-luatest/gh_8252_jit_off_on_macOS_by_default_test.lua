local t = require('luatest')

local g = t.group()

g.jit_off_on_macOS_by_default = function()
    t.assert_equals(jit.os == 'OSX', not jit.status(),
                    'JIT is disabled by default on macOS')
end
