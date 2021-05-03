#!/usr/bin/env tarantool

-- Just check whether all Lua sources related to jit.dump are
-- bundled to the binary. Otherwise, jit.dump module raises
-- an error that is handled via <pcall>.
-- XXX: pure require for jit.dump doesn't cover all the cases,
-- since dis_<arch>.lua are loaded at runtime. Furthermore, this
-- error is handled by JIT engine, so we can't use <pcall> for it.
-- Hence, simply sniff the output of the test to check that all
-- phases of trace compilation are dumped.

if #arg == 0 then
  local tap = require('tap')
  local test = tap.test('gh-5983-jit-library-smoke-tests')

  test:plan(1)

  -- XXX: Shell argument <test> is necessary to differ test case
  -- from the test runner.
  local cmd = string.gsub('<LUABIN> 2>&1 <SCRIPT> test', '%<(%w+)>', {
      LUABIN = arg[-1],
      SCRIPT = arg[0],
  })
  local proc = io.popen(cmd)
  local got = proc:read('*all'):gsub('^%s+', ''):gsub('%s+$', '')
  local expected = table.concat({
      '---- TRACE %d start',
      '---- TRACE %d IR',
      '---- TRACE %d mcode',
      '---- TRACE %d stop',
      '---- TRACE %d exit',
  }, '.+')

  test:like(got, expected , 'jit.dump smoke tests')

  os.exit(test:check() and 0 or 1)
end

-- Use *all* jit.dump options, so the test can check them all.
require('jit.dump').start('+tbisrmXaT')
-- Tune JIT engine to make the test faster and more robust.
jit.opt.start('hotloop=1')
-- Record primitive loop.
for _ = 1, 3 do end
