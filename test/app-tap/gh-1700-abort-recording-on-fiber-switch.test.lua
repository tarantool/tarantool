#!/usr/bin/env tarantool

if #arg == 0 then
  local checks = {
    {
      arg = {
        1, -- hotloop (arg[1])
        1, -- trigger (arg[2])
      },
      res = 'OK',
      msg = 'Trace is aborted',
    },
    {
      arg = {
        1, -- hotloop (arg[1])
        2, -- trigger (arg[2])
      },
      res = 'fiber %d+ is switched while running the compiled code %b()',
      msg = 'Trace is recorded',
    },
  }

  local tap = require('tap')
  local test = tap.test('gh-1700-abort-recording-on-fiber-switch')

  test:plan(#checks)

  local libext = package.cpath:match('?.(%a+);')
  local vars = {
    LUABIN = arg[-1],
    SCRIPT = arg[0],
    -- To support out-of-source build use relative paths in repo
    PATH   = arg[-1]:gsub('src/tarantool$', 'test/app-tap'),
    SUFFIX = libext,
  }

  local cmd = string.gsub('LUA_CPATH="$LUA_CPATH;<PATH>/?.<SUFFIX>" ' ..
                          'LUA_PATH="$LUA_PATH;<PATH>/?.lua" ' ..
                          ((libext == 'dylib' and 'DYLD' or 'LD') ..
                           '_LIBRARY_PATH=<PATH> ') ..
                          '<LUABIN> 2>&1 <SCRIPT>', '%<(%w+)>', vars)

  for _, ch in pairs(checks) do
    local res
    local proc = io.popen((cmd .. (' %s'):rep(#ch.arg)):format(unpack(ch.arg)))
    for s in proc:lines() do res = s end
    assert(res, 'proc:lines failed')
    test:like(res, ch.res, ch.msg)
  end

  os.exit(test:check() and 0 or 1)
end


-- Test body.

local cfg = {
  hotloop = arg[1] or 1,
  trigger = arg[2] or 1,
}

local ffi = require('ffi')
local ffiyield = ffi.load('libyield')
ffi.cdef('void yield(struct yield *state, int i)')

-- Set the value to trigger <yield> call switch the running fuber.
local yield = require('libyield')(cfg.trigger)

-- Depending on trigger and hotloop values the following contexts
-- are possible:
-- * if trigger <= hotloop -> trace recording is aborted
-- * if trigger >  hotloop -> trace is recorded but execution
--   leads to panic
jit.opt.start("3", string.format("hotloop=%d", cfg.hotloop))

for i = 0, cfg.trigger + cfg.hotloop do
  ffiyield.yield(yield, i)
end
-- Panic didn't occur earlier.
print('OK')
