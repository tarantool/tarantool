test_run = require('test_run').new()

--
-- gh-3228: Check that recursive structures are serialized
-- properly.
--
setmetatable({}, {__serialize = function(a) return a end})
setmetatable({}, {__serialize = function(a) return {a} end})
setmetatable({}, {__serialize = function(a) return {a, a} end})
setmetatable({}, {__serialize = function(a) return {a, a, a} end})
setmetatable({}, {__serialize = function(a) return {{a, a}, a} end})
setmetatable({}, {__serialize = function(a) return {a, 1} end})
setmetatable({}, {__serialize = function(a) return {{{{a}}}} end})

b = {}
setmetatable({b}, {__serialize = function(a) return {a_1 = a, a_2 = a, b_1 = b, b_2 = b} end})
setmetatable({b}, {__serialize = function(a) return {a_1 = a, a_2 = {a, b}, b = b} end})

a = {}
a[a] = a
recf = function(t) return setmetatable({}, {__serialize = recf}) end
setmetatable(a, {__serialize = recf}) return a

--
-- __serialize function is pure, i.e. always returns identical
-- value for identical argument. Otherwise, the behavior is
-- undefined. So that, we ignore the side effects and just use the
-- value after the first serialization.
--
a = {}
b = {}
b[a] = a
show_a = true
test_run:cmd('setopt delimiter ";"')
serialize = function()
    show_a = not show_a
    if show_a then
        return "a"
    else
        return "b" end
end;
test_run:cmd('setopt delimiter ""');
setmetatable(a, {__serialize = serialize})
b

test_run:cmd('setopt delimiter ";"')
function example()
    local a = {}
    local b = {}
    b[a] = a
    local reta
    local retb
    local function swap_ser(o)
        local newf
        local f = getmetatable(o).__serialize
        if f == reta then
            newf = retb
        else
            newf = reta
        end
        getmetatable(o).__serialize = newf
    end
    reta = function(o) swap_ser(o) return "a" end
    retb = function(o) swap_ser(o) return "b" end
    setmetatable(a, {__serialize = reta})
    return b
end;
test_run:cmd('setopt delimiter ""');
example()

--
-- Check the case, when "__serialize" returns nil.
--
setmetatable({}, {__serialize = function(t) return nil end})
