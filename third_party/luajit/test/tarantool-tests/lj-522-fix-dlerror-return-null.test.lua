local tap = require('tap')
local test = tap.test('lj-522-fix-dlerror-return-null'):skipcond({
  -- XXX: Unfortunately, it's too hard to overload (or even
  -- impossible, who knows, since Cupertino fellows do not
  -- provide any information about their system) something from
  -- dyld sharing cache (includes the <libdyld.dylib> library
  -- providing `dlerror()`).
  -- All in all, this test checks the part that is common for all
  -- platforms, so it's not vital to run this test on macOS since
  -- everything can be checked on Linux in a much easier way.
  ['<dlerror> cannot be overridden on macOS'] = jit.os == 'OSX',
})

test:plan(1)

-- `makecmd()` runs <%testname%/script.lua> by
-- `LUAJIT_TEST_BINARY` with the given environment and launch
-- options.
local script = require('utils').exec.makecmd(arg, {
  env = { LD_PRELOAD = 'mydlerror.so' },
  redirect = '2>&1',
})

local output = script()
test:like(output, 'dlopen failed', 'correct error message')

test:done(true)
