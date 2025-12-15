local tap = require('tap')
-- Test file to demonstrate assertion after `string.char()`
-- recording.
-- See also, https://github.com/tarantool/tarantool/issues/6371.
local test = tap.test('gh-6371-string-char-no-arg'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

-- XXX: Number of loop iterations.
-- * 1 -- instruction becomes hot.
-- * 2 -- recording of the loop body.
-- * 3 -- required for trace finalization, but this iteration
--        runs the generated mcode and reproduces the issue.
local NTEST = 3
test:plan(NTEST)

-- Storage for the results to avoid trace aborting by `test:ok()`.
-- XXX: Use `table.new()` here to avoid side exit from trace by
-- table resizing.
local results = require('table.new')(3, 0)
jit.opt.start('hotloop=1')
for _ = 1, NTEST do
  table.insert(results, string.char())
end

for i = 1, NTEST do
  test:ok(results[i] == '', 'correct recording of string.char() without args')
end

test:done(true)
