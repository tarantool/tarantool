local tap = require('tap')
local ffi = require('ffi')
local test  = tap.test('lj-1004-oom-error-frame'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Test requires GC64 mode disabled'] = ffi.abi('gc64'),
  ['Disabled on MacOS due to #8652'] = jit.os == 'OSX',
})

test:plan(2)

local testoomframe = require('testoomframe')

collectgarbage()

local anchor_memory = {} -- luacheck: no unused
local function eatchunks(size)
  while true do
    anchor_memory[ffi.new('char[?]', size)] = 1
  end
end

-- The chunk size below is empirical. It is big enough, so the
-- test is not too long, yet small enough for the OOM frame issue
-- to have enough iterations in the second loop to trigger.
pcall(eatchunks, 512 * 1024 * 1024)

local anchor = {}
local function extra_frame(val)
  table.insert(anchor, val)
end

local function chomp()
  while true do
    extra_frame(testoomframe.allocate_userdata())
  end
end

local st, err = pcall(chomp)

-- Prevent OOM outside of the protected frame.
anchor_memory = nil
collectgarbage()

test:ok(st == false, 'on-trace error handled successfully')
test:like(err, 'not enough memory', 'error is OOM')
test:done(true)
