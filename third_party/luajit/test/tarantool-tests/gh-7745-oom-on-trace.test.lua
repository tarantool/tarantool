local tap = require('tap')
local ffi = require('ffi')

local test = tap.test('OOM on trace'):skipcond({
  ['Broken unwiding in tarantool_panic_handler'] = _TARANTOOL and
                                                   (jit.os == 'OSX'),
  ['Disabled on MacOS due to #8652'] = jit.os == 'OSX',
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled with Valgrind (Timeout)'] = os.getenv('LUAJIT_TEST_USE_VALGRIND'),
})

test:plan(1)

-- NB: When GC64 is enabled, fails with TABOV, otherwise -- with
-- OOM.
local function memory_payload()
  local t = {} -- luacheck: no unused
  for i = 1, 1e10 do
    t[ffi.new('uint64_t')] = i
  end
end

local anchor = {} -- luacheck: no unused
local function eatchunks(size)
  while true do
    anchor[ffi.new('char[?]', size)] = 1
  end
end

if not ffi.abi('gc64') then
  pcall(eatchunks, 64 * 1024 * 1024)
end

local res = pcall(memory_payload)

-- Free memory for `test:ok()`.
anchor = nil
collectgarbage()

test:ok(res == false)

test:done(true)
