local tap = require('tap')
local test = tap.test('lj-flush-on-trace'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
  -- XXX: This test has to check the particular patch for
  -- <mcode_protfail>, and <mprotect> is overloaded for this
  -- purpose. However, <mprotect> is widely used in Tarantool
  -- to play with fiber stacks, so overriding <mprotect> is not
  -- suitable to test this feature in Tarantool.
  ['<mprotect> overriding can break Tarantool'] = _TARANTOOL,
  -- XXX: Unfortunately, it's too hard to overload (or even
  -- impossible, who knows, since Cupertino fellows do not
  -- provide any information about their system) something from
  -- libsystem_kernel.dylib (the library providing <mprotect>).
  -- All in all, this test checks the part that is common for all
  -- platforms, so it's not vital to run this test on macOS since
  -- everything can be checked on Linux in a much easier way.
  ['<mprotect> cannot be overridden on macOS'] = jit.os == 'OSX',
})

test:plan(4)

-- <makecmd> runs %testname%/script.lua by <LUAJIT_TEST_BINARY>
-- with the given environment, launch options and CLI arguments.
local script = require('utils').exec.makecmd(arg, {
  env = { LD_PRELOAD = 'mymprotect.so' },
  redirect = '2>&1',
})

-- See the rationale for this poison hack in the script.lua.
local poison = '"runtime code generation succeed"'
local output = script(poison)
test:like(output, 'runtime code generation failed, restricted kernel%?',
          'Panic occurred as a result of <mprotect> failure')
test:unlike(output, 'Segmentation fault',
            'LuaJIT exited as a result of the panic (error check)')
test:unlike(output, 'Aborted',
            'LuaJIT exited as a result of the panic (assertion check)')
test:unlike(output, poison,
            'LuaJIT exited as a result of the panic (poison check)')

test:done(true)
