local tap = require('tap')
local test = tap.test('lj-549-bytecode-loader'):skipcond({
  -- XXX: Tarantool doesn't use default LuaJIT loaders, and Lua
  -- bytecode can't be loaded from the shared library. For more
  -- info: https://github.com/tarantool/tarantool/issues/9671.
  ['Test uses exotic type of loaders (see #9671)'] = _TARANTOOL,
})

test:plan(2)

-- Test creates a shared library with LuaJIT bytecode,
-- loads shared library as a Lua module and checks,
-- that no crashes eliminated.
--
-- Manual steps for reproducing are the following:
--
-- $ make HOST_CC='gcc -m32' TARGET_CFLAGS='-m32' \
--                           TARGET_LDFLAGS='-m32' \
--                           TARGET_SHLDFLAGS='-m32' \
--                           -f Makefile.original
-- $ echo 'print("test")' > a.lua
-- $ LUA_PATH="src/?.lua;;" luajit -b a.lua a.c
-- $ gcc -m32 -fPIC -shared a.c -o a.so
-- $ luajit -e "require('a')"
-- Program received signal SIGBUS, Bus error

local module_name = 'script'
local ok, module = pcall(require, module_name)
test:is(ok, true, 'bytecode loader works')
test:is(module.msg, 'Lango team', 'message is ok')

test:done(true)
