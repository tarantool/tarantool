local tap = require('tap')
local test = tap.test('gh-5994-memprof-human-readable'):skipcond({
  ['Profile tools are implemented for x86_64 only'] = jit.arch ~= 'x86' and
                                                      jit.arch ~= 'x64',
  ['Profile tools are implemented for Linux only'] = jit.os ~= 'Linux',
  -- XXX: Tarantool integration is required to run this test
  -- properly.
  ['No profile tools CLI option integration'] = _TARANTOOL,
  -- See also https://github.com/LuaJIT/LuaJIT/issues/606.
  ['Disabled due to LuaJIT/LuaJIT#606'] = os.getenv('LUAJIT_TABLE_BUMP'),
  ['Memprof is disabled'] = os.getenv('LUAJIT_DISABLE_MEMPROF'),
})

local utils = require('utils')
local TMP_BINFILE_MEMPROF = utils.tools.profilename('memprofdata.tmp.bin')
local PARSE_CMD = utils.exec.luacmd(arg) .. ' -tm '

local function generate_output(bytes)
  local res, err = misc.memprof.start(TMP_BINFILE_MEMPROF)
  -- Should start successfully.
  assert(res, err)

  -- luacheck: no unused
  local _ = string.rep('_', bytes)

  res, err = misc.memprof.stop()
  -- Should stop successfully.
  assert(res, err)
end

local TEST_SET = {
  {
    bytes = 2049,
    match = '%dB',
    hr = false,
    name = 'non-human-readable mode is correct'
  },
  {
    bytes = 100,
    match = '%dB',
    hr = true,
    name = 'human-readable mode: bytes'
  },
  {
    bytes = 2560,
    match = '%d+%.%d%dKiB',
    hr = true,
    name = 'human-readable mode: float'
  },
  {
    bytes = 2048,
    match = '%dKiB',
    hr = true,
    name = 'human-readable mode: KiB'
  },
  {
    bytes = 1024 * 1024,
    match = '%dMiB',
    hr = true,
    name = 'human-readable mode: MiB'
  },
  -- XXX: The test case for GiB is not implemented because it is
  -- OOM-prone for non-GC64 builds.
}

test:plan(#TEST_SET)

for _, params in ipairs(TEST_SET) do
  generate_output(params.bytes)
  local cmd = PARSE_CMD .. (params.hr and ' --human-readable ' or '')
  local output = io.popen(cmd .. TMP_BINFILE_MEMPROF):read('*all')
  test:like(output, params.match, params.name)
end

os.remove(TMP_BINFILE_MEMPROF)
test:done(true)
