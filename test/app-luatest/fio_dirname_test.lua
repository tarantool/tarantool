local t = require('luatest')
local fio = require('fio')

local g = t.group()

g.test_dirname = function()
    local cases = {
        {'', '.'},
        {'.', '.'},
        {'..', '.'},
        {'a', '.'},
        {'a/', '.'},
        {'a/b/', 'a'},
        {'a//b', 'a'},
        {'/a//b', '/a'},
        {'/', '/'},
        {'//', '/'},
        {'///a', '/'},
    }
    for _, case in ipairs(cases) do
        t.assert_equals(fio.dirname(case[1]), case[2])
    end
end

g.test_long_path = function()
    local dir = string.rep('a', 1024)
    local path = dir .. '/b'
    t.assert_equals(fio.dirname(path), dir)
end
