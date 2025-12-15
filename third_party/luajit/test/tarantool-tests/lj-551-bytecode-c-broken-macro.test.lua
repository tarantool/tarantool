local tap = require('tap')
local test = tap.test('lj-551-bytecode-c-broken-macro'):skipcond({
  -- XXX: Tarantool doesn't use default LuaJIT loaders, and Lua
  -- bytecode can't be loaded from the shared library. For more
  -- info: https://github.com/tarantool/tarantool/issues/9671.
  ['Test uses exotic type of loaders (see #9671)'] = _TARANTOOL,
})

test:plan(4)

local function check_module(t, module_name)
  local ok, module = pcall(require, module_name)
  local message = ('symbol %q is available in a library'):format(module_name)
  t:ok(ok, message)
  t:is(module.msg, 'Lango team')
end

check_module(test, 'script_c')
check_module(test, 'script_cc')

test:done(true)
