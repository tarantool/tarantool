local buffer = require('buffer')
local t = require('luatest')
local g = t.group()

local function fill_memory(memory, size)
    for i = 0, size - 1 do
        memory[i] = i
    end
end

local function check_memory(memory, size)
    for i = 0, size - 1 do
        if memory[i] ~= i then return false end
    end
    return true
end

g.test_object_misc = function()
    local ibuf = buffer.ibuf()
    local prbuf_size = 100
    local memory = ibuf:alloc(prbuf_size)
    local prbuf = buffer.prbuf_create(memory, prbuf_size)

    local sample_size = 4
    local entry_count = 5
    for _ = 1, entry_count do
        local raw = prbuf:prepare(sample_size)
        t.assert_equals(raw ~= nil, true)
        fill_memory(raw, sample_size)
        prbuf:commit()
    end

    local prbuf_recovered = buffer.prbuf_open(memory)
    local iter = prbuf_recovered:iterator_create()
    entry_count = 0
    local entry = iter:next()
    while entry ~= nil do
        entry_count = entry_count + 1
        t.assert_equals(entry.size, sample_size)
        local data = entry:data()
        t.assert_equals(string.len(data), entry.size)
        t.assert_equals(data, require('ffi').string(entry.ptr, entry.size))
        t.assert_equals(check_memory(entry.ptr, tonumber(entry.size)), true)
        entry = iter:next()
    end
    t.assert_equals(entry_count, 5)
end
