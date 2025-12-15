local tap = require('tap')
-- Test file to demonstrate LuaJIT incorrect behaviour with missed
-- closing a file handle for the profile output file.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1304.
local test = tap.test('lj-1304-close-profile-dump-with-0-samples')
local profilename = require('utils').tools.profilename

test:plan(2)

local filename = profilename('sysprof')
-- An interval of the profiling is set to a huge enough value to
-- be sure that there are no samples collected.
local jit_p_options = 'i9999999'

local function close_profile_dump_with_0_samples(subtest)
  local jit_p = require('jit.p')

  subtest:plan(1)

  collectgarbage('stop')

  jit_p.start(jit_p_options, filename)
  jit_p.stop()

  local f = io.open(filename, 'r')
  local p_content = f:read('a*')
  subtest:is(p_content, '[No samples collected]\n',
             'profile dump has no samples')
  f:close()

  -- Teardown.
  collectgarbage('restart')
  os.remove(filename)
end

local function close_profile_dump_with_0_samples_with_unload(subtest)
  local jit_p = require('jit.p')

  subtest:plan(1)

  collectgarbage('stop')
  jit_p.start(jit_p_options, filename)
  jit_p.stop()

  -- Unload the module and clean the local.
  package.loaded['jit.p'] = nil
  jit_p = nil -- luacheck: no unused
  collectgarbage('collect')

  local f = io.open(filename, 'r')
  local p_content = f:read('a*')
  subtest:is(p_content, '[No samples collected]\n',
             'profile dump has no samples')
  f:close()

  -- Teardown.
  collectgarbage('restart')
  os.remove(filename)
end

test:test('close profile dump with 0 samples',
          close_profile_dump_with_0_samples)
test:test('close profile dump with 0 samples and profile module unload',
          close_profile_dump_with_0_samples_with_unload)

test:done(true)
