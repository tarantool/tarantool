local tap = require('tap')
local test = tap.test('gh-9217-profile-parsers-error-handling'):skipcond({
  ['Profile tools are implemented for x86_64 only'] = jit.arch ~= 'x86' and
                                                      jit.arch ~= 'x64',
  ['Profile tools are implemented for Linux only'] = jit.os ~= 'Linux',
  -- XXX: Tarantool integration is required to run this test
  -- properly.
  ['No profile tools CLI option integration'] = _TARANTOOL,
  -- See also https://github.com/LuaJIT/LuaJIT/issues/606.
  ['Disabled due to LuaJIT/LuaJIT#606'] = os.getenv('LUAJIT_TABLE_BUMP'),
  ['Sysprof is disabled'] = os.getenv('LUAJIT_DISABLE_SYSPROF'),
})

jit.off()
jit.flush()

local table_new = require('table.new')
local utils = require('utils')

local BAD_PATH = utils.tools.profilename('bad-path-tmp.bin')
local NON_PROFILE_DATA = utils.tools.profilename('not-profile-data.tmp.bin')
local CORRUPT_PROFILE_DATA = utils.tools.profilename('profdata.tmp.bin')

local EXECUTABLE = utils.exec.luacmd(arg)
local PARSERS = {
  memprof = EXECUTABLE .. ' -tm ',
  sysprof = EXECUTABLE .. ' -ts ',
}
local REDIRECT_OUTPUT = ' 2>&1'

local TABLE_SIZE = 20

local TEST_CASES = {
  {
    path = BAD_PATH,
    err_msg = 'Failed to open'
  },
  {
    path = NON_PROFILE_DATA,
    err_msg = 'Failed to parse symtab from'
  },
  {
    path = CORRUPT_PROFILE_DATA,
    err_msg = 'Failed to parse profile data from'
  },
}

test:plan(2 * #TEST_CASES)

local function generate_non_profile_data(path)
  local file = io.open(path, 'w')
  file:write('data')
  file:close()
end

local function generate_corrupt_profile_data(path)
  local res, err = misc.memprof.start(path)
  -- Should start successfully.
  assert(res, err)

  local _ = table_new(TABLE_SIZE, 0)
   _ = nil
  collectgarbage()

  res, err = misc.memprof.stop()
  -- Should stop successfully.
  assert(res, err)

  local file = io.open(path, 'r')
  local content = file:read('*all')
  file:close()
  local index = string.find(content, 'ljm')

  file = io.open(path, 'w')
  file:write(string.sub(content, 1, index - 1))
  file:close()
end

generate_non_profile_data(NON_PROFILE_DATA)
generate_corrupt_profile_data(CORRUPT_PROFILE_DATA)

for _, case in ipairs(TEST_CASES) do
  for profiler, parser in pairs(PARSERS) do
    local path = case.path
    local err_msg = case.err_msg
    local output = io.popen(parser .. path .. REDIRECT_OUTPUT):read('*all')
    test:like(output, err_msg, string.format('%s: %s', profiler, err_msg))
  end
end

os.remove(NON_PROFILE_DATA)
os.remove(CORRUPT_PROFILE_DATA)
test:done(true)
