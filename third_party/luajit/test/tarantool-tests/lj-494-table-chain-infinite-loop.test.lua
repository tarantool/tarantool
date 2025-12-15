local tap = require('tap')

local test = tap.test("lj-494-table-chain-infinite-loop")
test:plan(1)

-- Test file to demonstrate Lua table hash chain bugs discussed in
--     https://github.com/LuaJIT/LuaJIT/issues/494
-- Credit: prepared by Peter Cawley here with minor edits:
-- https://gist.github.com/corsix/1fc9b13a2dd5f3659417b62dd54d4500

--- Plumbing
local ffi = require("ffi")
ffi.cdef("char* strstr(const char*, const char*)")
local strstr = ffi.C.strstr
local cast = ffi.cast
local str_hash_offset = cast("uint32_t*", strstr("*", ""))[-2] == 1 and 3 or 2
local function str_hash(s)
    return cast("uint32_t*", strstr(s, "")) - str_hash_offset
end
local table_new = require("table.new")

--- Prepare some objects
local victims = {}
local orig_hash = {}
for c in ("abcdef"):gmatch"." do
    local v = c .. "{09add58a-13a4-44e0-a52c-d44d0f9b2b95}"
    victims[c] = v
    orig_hash[c] = str_hash(v)[0]
end
collectgarbage()

do --- Basic version of the problem
    for _, v in pairs(victims) do
        str_hash(v)[0] = 0
    end
    local t = table_new(0, 8)
    -- Make chain a -> b -> c -> d, all with a as primary
    t[victims.a] = true
    t[victims.d] = true
    t[victims.c] = true
    t[victims.b] = true
    -- Change c's primary to b, and d's primary to c
    t[victims.d] = nil
    t[victims.c] = nil
    str_hash(victims.c)[0] = 5
    str_hash(victims.d)[0] = 6
    t[victims.c] = true
    t[victims.d] = true
    -- Insert something with b as primary
    str_hash(victims.e)[0] = 5
    t[victims.e] = true
    -- Check for consistency
    for c in ("abcde"):gmatch"." do
        assert(t[victims[c]], c)
    end
end
collectgarbage()

do --- Just `mn != freenode` can lead to infinite loops
    for _, v in pairs(victims) do
        str_hash(v)[0] = 0
    end
    local t = table_new(0, 8)
    -- Make chain a -> b -> c -> d, all with a as primary
    t[victims.a] = true
    t[victims.d] = true
    t[victims.c] = true
    t[victims.b] = true
    -- Change c's primary to b, and d's primary to d
    t[victims.d] = nil
    t[victims.c] = nil
    str_hash(victims.c)[0] = 5
    str_hash(victims.d)[0] = 7
    t[victims.c] = true
    t[victims.d] = true
    -- Insert something with b as primary
    str_hash(victims.e)[0] = 5
    t[victims.e] = true
    -- Insert something with d as primary (infinite lookup loop)
    str_hash(victims.f)[0] = 7
    t[victims.f] = true
end
collectgarbage()

do --- Just `mn != nn` can lead to infinite loops
    for _, v in pairs(victims) do
        str_hash(v)[0] = 0
    end
    local t = table_new(0, 8)
    -- Make chain a -> b -> c -> d -> e, all with a as primary
    t[victims.a] = true
    t[victims.e] = true
    t[victims.d] = true
    t[victims.c] = true
    t[victims.b] = true
    -- Change c's primary to b, d's primary to d, and e's primary
    -- to d
    t[victims.e] = nil
    t[victims.d] = nil
    t[victims.c] = nil
    str_hash(victims.c)[0] = 4
    str_hash(victims.d)[0] = 6
    str_hash(victims.e)[0] = 6
    t[victims.c] = true
    t[victims.d] = true
    t[victims.e] = true
    -- Insert something with b as primary (infinite rechaining
    -- loop)
    str_hash(victims.f)[0] = 4
    t[victims.f] = true
end

for i = 0, 10 do --- Non-strings can need rechaining too
    collectgarbage()

    local k = tonumber((("0x%xp-1074"):format(i)))
    str_hash(victims.a)[0] = 0
    str_hash(victims.b)[0] = 0
    local t = table_new(0, 4)
    -- a -> b, both with a as primary
    t[victims.a] = true
    t[victims.b] = true
    -- Change b's primary to b
    t[victims.b] = nil
    str_hash(victims.b)[0] = 3
    t[victims.b] = true
    -- Might get a -> b -> k, with k's primary as b
    t[k] = true
    -- Change b's primary to a
    t[victims.b] = nil
    str_hash(victims.b)[0] = 0
    t[victims.b] = true
    -- Insert something with b as primary
    str_hash(victims.c)[0] = 3
    t[victims.c] = true
    -- Check for consistency
    assert(t[k], i)
end

for i = 0, 10 do --- Non-strings can be moved to freenode
    collectgarbage()

    local k = false
    str_hash(victims.a)[0] = 0
    str_hash(victims.b)[0] = 0
    local t = table_new(0, 4)
    -- a -> k -> b, all with a as primary
    t[victims.a] = true
    t[victims.b] = true
    t[k] = true
    -- Change b's primary to k
    t[victims.b] = nil
    str_hash(victims.b)[0] = 2
    t[victims.b] = true
    -- Insert a non-string with primary of k
    t[tonumber((("0x%xp-1074"):format(i)))] = true
    -- Check for consistency
    assert(t[victims.b], i)
end
collectgarbage()

do --- Do not forget to advance freenode in the not-string case
    local t = table_new(0, 4)
    -- Chain of colliding numbers
    t[0x0p-1074] = true
    t[0x4p-1074] = true
    t[0x8p-1074] = true
    -- Steal middle node of the chain to be a main node (infinite
    -- walking loop)
    t[0x2p-1074] = true
end
collectgarbage()

--- Restore interpreter invariants, just in case
for c, v in pairs(victims) do
    str_hash(v)[0] = orig_hash[c]
end
test:ok(true,
        "table keys collisions are resolved properly (no assertions failed)")

test:done(true)
