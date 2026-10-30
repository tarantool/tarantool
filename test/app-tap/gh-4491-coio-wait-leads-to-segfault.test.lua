#!/usr/bin/env tarantool

local tap = require('tap')

local test = tap.test('gh-4491-coio-wait-leads-to-segfault')

-- Test file to demonstrate platform failure due to fiber switch
-- while trace recording, details:
--     https://github.com/tarantool/tarantool/issues/4491

local fiber = require('fiber')
local ffi = require('ffi')
ffi.cdef('int coio_wait(int fd, int event, double timeout);')

local cfg = {
  hotloop = arg[1] or 1,
  fibers = arg[1] or 2,
  timeout = { put = 1, get = 1 },
}

test:plan(cfg.fibers + 1)

local args = {
  fd      = 1   , -- STDIN file descriptor
  event   = 0x1 , -- COIO_READ event
  timeout = 0.05, -- Timeout value
}

local function run(iterations, channel)
  for _ = 1, iterations do
    ffi.C.coio_wait(args.fd, args.event, args.timeout)
  end
  channel:put(true, cfg.timeout.put)
end

local channels = { }

jit.opt.start('3', string.format('hotloop=%d', cfg.hotloop))

for _ = 1, cfg.fibers do
  channels[_] = fiber.channel(1)
  fiber.new(run, cfg.hotloop + 1, channels[_])
end

-- Finalize the existing fibers
for _ = 1, cfg.fibers do
  test:ok(channels[_]:get(cfg.timeout.get),
          string.format('fiber #%d successfully finished', _))
end

test:ok(true, 'trace is not recorded due to fiber switch underneath coio_wait')

os.exit(test:check() and 0 or 1)
