local tap = require('tap')
local test = tap.test('gh-5688-tool-cli-flag'):skipcond({
  ['Profile tools are implemented for x86_64 only'] = jit.arch ~= 'x86' and
                                                      jit.arch ~= 'x64',
  ['Profile tools are implemented for Linux only'] = jit.os ~= 'Linux',
  -- XXX: Tarantool integration is required to run this test
  -- properly.
  ['No profile tools CLI option integration'] = _TARANTOOL,
  -- See also https://github.com/LuaJIT/LuaJIT/issues/606.
  ['Disabled due to LuaJIT/LuaJIT#606'] = os.getenv('LUAJIT_TABLE_BUMP'),
  ['Sysprof is disabled'] = os.getenv('LUAJIT_DISABLE_SYSPROF'),
  -- See also https://github.com/tarantool/tarantool/issues/10803.
  ['Disabled due to #10803'] = os.getenv('LUAJIT_TEST_USE_VALGRIND'),
})

test:plan(3)

jit.off()
jit.flush()

local table_new = require('table.new')
local utils = require('utils')

local BAD_PATH = utils.tools.profilename('bad-path-tmp.bin')
local TMP_BINFILE_MEMPROF = utils.tools.profilename('memprofdata.tmp.bin')
local TMP_BINFILE_SYSPROF = utils.tools.profilename('sysprofdata.tmp.bin')

local EXECUTABLE = utils.exec.luacmd(arg)
local MEMPROF_PARSER = EXECUTABLE .. ' -tm '
local SYSPROF_PARSER = EXECUTABLE .. ' -ts '

local REDIRECT_OUTPUT = ' 2>&1'

local TABLE_SIZE = 20

local SMOKE_CMD_SET = {
  {
    cmd = EXECUTABLE .. ' -t ' .. BAD_PATH,
    like = '.+Available tools.+',
  },
  {
    cmd = EXECUTABLE .. ' -ta ' .. BAD_PATH,
    like = '.+Available tools.+',
  },
}

local MEMPROF_CMD_SET = {
  {
    cmd = MEMPROF_PARSER .. BAD_PATH,
    like = 'Failed to open.*fopen, errno: 2',
  },
  {
    cmd = MEMPROF_PARSER .. TMP_BINFILE_MEMPROF,
    like = 'ALLOCATIONS.+',
  },
  {
    cmd = MEMPROF_PARSER .. ' --wrong ' .. TMP_BINFILE_MEMPROF,
    like = 'unrecognized option',
  },
  {
    cmd = MEMPROF_PARSER .. ' --leak-only ' .. TMP_BINFILE_MEMPROF,
    like = 'HEAP SUMMARY:.+',
  },
}

local SYSPROF_CMD_SET = {
  {
    cmd = SYSPROF_PARSER .. BAD_PATH,
    like = 'Failed to open.*fopen, errno: 2',
  },
  {
    cmd = SYSPROF_PARSER .. TMP_BINFILE_SYSPROF,
    like = '[%w_@:;]+%s%d+\n*.*',
  },
  {
    cmd = SYSPROF_PARSER .. ' --wrong ' .. TMP_BINFILE_SYSPROF,
    like = 'unrecognized option',
  },
}

local function memprof_payload()
  local _ = table_new(TABLE_SIZE, 0)
   _ = nil
  collectgarbage()
end

local function sysprof_payload()
  local function fib(n)
    if n <= 1 then
      return n
    end
    return fib(n - 1) + fib(n - 2)
  end
  return fib(32)
end

local function generate_profiler_output(opts, payload, profiler)
  local res, err = profiler.start(opts)
  -- Should start successfully.
  assert(res, err)

  payload()

  res, err = profiler.stop()
  -- Should stop successfully.
  assert(res, err)
end

local function tool_test_case(case_name, cmd_set)
  test:test(case_name, function(subtest)
    subtest:plan(#cmd_set)

    for idx = 1, #cmd_set do
      local output = io.popen(cmd_set[idx].cmd .. REDIRECT_OUTPUT):read('*all')
      subtest:like(output, cmd_set[idx].like, cmd_set[idx].cmd)
    end
  end)
end

tool_test_case('smoke', SMOKE_CMD_SET)

generate_profiler_output(TMP_BINFILE_MEMPROF, memprof_payload, misc.memprof)
tool_test_case('memprof parsing tool', MEMPROF_CMD_SET)

local sysprof_opts = { mode = 'C', path = TMP_BINFILE_SYSPROF }
generate_profiler_output(sysprof_opts, sysprof_payload, misc.sysprof)
tool_test_case('sysprof parsing tool', SYSPROF_CMD_SET)

os.remove(TMP_BINFILE_MEMPROF)
os.remove(TMP_BINFILE_SYSPROF)
test:done(true)
