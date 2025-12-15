-- The benchmark to check the performance of coroutine interaction
-- to test possible "death by concurrency," when one coroutine
-- is active and others are waiting their turn.
-- For the details see:
-- https://pybenchmarks.org/u64q/performance.php?test=threadring

local bench = require("bench").new(arg)

-- The Computer Language Benchmarks Game
-- http://shootout.alioth.debian.org/
-- contributed by Sam Roberts
-- reviewed by Bruno Massa

local n         = tonumber(arg and arg[1]) or 2e7

-- fixed size pool
local poolsize  = 503

-- cache these to avoid global environment lookups
local yield     = coroutine.yield

local body = function(token)
  while true do
    token = yield(token + 1)
  end
end

bench:add({
  name = "coroutine_ring",
  payload = function()
    -- Cache to avoid upvalue lookups.
    local token = 0
    local n = n
    local poolsize = poolsize

    -- Cache these to avoid global environment lookups.
    local create = coroutine.create
    local resume = coroutine.resume

    local id = 1
    local ok

    -- Create all threads.
    local threads = {}
    for id = 1, poolsize do
      threads[id] = create(body)
    end

    -- Send the token.
    repeat
      if id == poolsize then
        id = 1
      else
        id = id + 1
      end
      ok, token = resume(threads[id], token)
    until token == n
    return id
  end,
  checker = function(id) return id == (n % poolsize + 1) end,
  items = n,
})

bench:run_and_report()
