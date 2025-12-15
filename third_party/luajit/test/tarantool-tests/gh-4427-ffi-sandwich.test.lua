local tap = require('tap')
local test = tap.test('gh-4427-ffi-sandwich'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

test:plan(2)

-- <makecmd> runs %testname%/script.lua by <LUAJIT_TEST_BINARY>
-- with the given environment, launch options and CLI arguments.
local script = require('utils').exec.makecmd(arg, {
  -- XXX: Apple tries their best to "protect their users from
  -- malware". As a result SIP (see the link[1] below) has been
  -- designed and released. Now, Apple developers are so
  -- protected, that they can load nothing being not installed in
  -- the system, since the environment is sanitized before the
  -- child process is launched. In particular, environment
  -- variables starting with DYLD_ and LD_ are unset for child
  -- process. For more info, see the docs[2] below.
  --
  -- The environment variable below is used by FFI machinery to
  -- find the proper shared library.
  --
  -- luacheck: push no max comment line length
  --
  -- [1]: https://support.apple.com/en-us/HT204899
  -- [2]: https://developer.apple.com/library/archive/documentation/Security/Conceptual/System_Integrity_Protection_Guide/RuntimeProtections/RuntimeProtections.html
  --
  -- luacheck: pop
  env = { DYLD_LIBRARY_PATH = os.getenv('DYLD_LIBRARY_PATH') },
  redirect = '2>&1',
})

-- Depending on trigger and hotloop values the following contexts
-- are possible:
-- * if trigger <= hotloop -> trace recording is aborted
-- * if trigger >  hotloop -> trace is recorded but execution
--   leads to panic
local hotloop = 1
local cases = {
  abort = {
    trigger = hotloop,
    expected = '#4427 still works',
    test = 'is',
    message = 'Trace is aborted',
  },
  panic = {
    trigger = hotloop + 1,
    expected = 'Lua VM re%-entrancy is detected while executing the trace',
    test = 'like',
    message = 'Trace is compiled',
  },
}

for _, subtest in pairs(cases) do
  local output = script(hotloop, subtest.trigger)
  -- XXX: explicitly pass <test> as an argument to <testf>
  -- to emulate test:is(...), test:like(...), etc.
  test[subtest.test](test, output, subtest.expected, subtest.message)
end

test:done(true)
