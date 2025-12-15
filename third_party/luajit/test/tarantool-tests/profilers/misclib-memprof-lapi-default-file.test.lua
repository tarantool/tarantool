local tap = require('tap')
local test = tap.test('misc-memprof-lapi-default-file'):skipcond({
  ['Memprof is implemented for x86_64 only'] = jit.arch ~= 'x86' and
                                               jit.arch ~= 'x64',
  ['Memprof is disabled'] = os.getenv('LUAJIT_DISABLE_MEMPROF'),
})

test:plan(1)

local tools = require('utils.tools')

test:test('default-output-file', function(subtest)

  subtest:plan(1)

  local default_output_file = 'memprof.bin'
  os.remove(default_output_file)

  local res, err = misc.memprof.start()
  -- Want to cleanup carefully if something went wrong.
  if not res then
    test:fail('sysprof was not started: ' .. err)
    os.remove(default_output_file)
  end

  res, err = misc.memprof.stop()
  -- Want to cleanup carefully if something went wrong.
  if not res then
    test:fail('sysprof was not started: ' .. err)
    os.remove(default_output_file)
  end


  local profile_buf = tools.read_file(default_output_file)
  subtest:ok(profile_buf ~= nil and #profile_buf ~= 0,
             'default output file is not empty')

  -- We don't need it anymore.
  os.remove(default_output_file)
end)

test:done(true)
