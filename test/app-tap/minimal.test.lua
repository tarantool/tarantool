#!/usr/bin/env tarantool

print('Hello, World!')

--
-- Command-line argument handling
--
local script = io.open('script-args.lua', 'w')
script:write([[
-- Tarantool binary
print('arg[-1]', arg[-1]:match('tarantool'))
-- Script name
print('arg[0] ', arg[0])
-- Command-line arguments
print('arg', arg[1], arg[2], arg[3])
print('...', ...)
]])
script:close()

io.flush()

os.execute("tarantool ./script-args.lua 1 2 3")

--
-- LUA_PATH and LUA_CPATH argument handling
--
local script = io.open('script-path.lua', 'w')
script:write([[
print(package.path)
os.exit(0)
]])
script:close()
local script = io.open('script-cpath.lua', 'w')
script:write([[
print(package.cpath)
os.exit(0)
]])
script:close()

io.flush()

-- gh-1428: Ensure that LUA_PATH/LUA_CPATH have the same behaviour, as in
-- LuaJIT/Lua
local tap = require('tap').test('lua_path/lua_cpath')
tap:plan(8)

for _, env in ipairs({
    {'LUA_PATH', 'script-path.lua', package.path},
    {'LUA_CPATH', 'script-cpath.lua', package.cpath}
}) do
    for _, res in ipairs({
        {' is empty', '', ''},
        {' isn\'t empty (without ";;")', 'bla-bla.lua',      'bla-bla.lua'            },
        {' isn\'t empty (without ";;")', 'bla-bla.lua;',     'bla-bla.lua;'           },
        {' isn\'t empty (with ";;")',    'bla-bla.lua;.*;;', 'bla-bla.lua;' .. env[3] },
    }) do
        local cmd = table.concat({
            ("%s='%s'"):format(env[1], res[2]),
            ('tarantool %s'):format(env[2]),
        }, ' ')
        local fh = io.popen(cmd)
        local rv = fh:read():gsub('-', '%%-'):gsub('+', '%%+'):gsub('?', '%%?')
        tap:like(res[3], rv, env[1] .. res[1])
        fh:close()
    end
end

tap:check()
