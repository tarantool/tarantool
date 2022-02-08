local t = require('luatest')
local fio = require('fio')
local g = t.group()


local function readfile(filename)
    local f = assert(io.open(filename, "rb"))
    return f:read('*a')
end

local files = {
    'strict',
    'debug',
    'errno',
    'fiber',
    'env',
    'datetime',
}

-- calculate reporsitory root using directory of a current
-- script, but get 2 directories above
local function repo_root()
    local myself = fio.abspath(debug.getinfo(1,'S').source:gsub("^@", ""))
    local root = fio.abspath(fio.dirname(myself) .. '/../..')
    return root
end

g.test_tarantool_debug_getsources = function()
    local git_root = repo_root()
    t.assert_is_not(git_root, nil)
    t.assert(fio.stat(git_root):is_dir())
    local lua_src_dir = fio.pathjoin(git_root, '/src/lua')
    t.assert_is_not(lua_src_dir, nil)
    t.assert(fio.stat(lua_src_dir):is_dir())

    local tnt = require('tarantool')
    t.assert_is_not(tnt, nil)
    local luadebug = tnt.debug
    t.assert_is_not(luadebug, nil)

    for _, file in pairs(files) do
        local path = ('%s/%s.lua'):format(lua_src_dir, file)
        local text = readfile(path)
        t.assert_is_not(text, nil)
        local source = luadebug.getsources(file)
        t.assert_equals(source, text)
        source = luadebug.getsources(('@builtin/%s.lua'):format(file))
        t.assert_equals(source, text)
    end
end
