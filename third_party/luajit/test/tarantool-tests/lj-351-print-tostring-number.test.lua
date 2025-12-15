local tap = require('tap')

local test = tap.test('lj-351-print-tostring-number')
test:plan(8)

local script = require('utils').exec.makecmd(arg)

local cases = {
  {typename = 'nil', value = 'nil'},
  {typename = 'boolean', value = 'true'},
  {typename = 'number', value = '42'},
  -- FIXME: Test case below is disabled, because __tostring
  -- metamethod isn't checked for string base metatable.
  -- See also https://github.com/tarantool/tarantool/issues/6746.
  -- {typename = 'string', value = '[[teststr]]'},
  {typename = 'table', value = '{}'},
  {typename = 'function', value = 'function() end'},
  {typename = 'userdata', value = 'newproxy()'},
  {typename = 'thread', value = 'coroutine.create(function() end)'},
  {typename = 'cdata', value = '1ULL'}
}

for _, subtest in pairs(cases) do
  local output = script(('"%s"'):format(subtest.value), subtest.typename)
  test:is(output, ('__tostring is reloaded for %s'):format(subtest.typename),
          ('subtest is OK for %s type'):format(subtest.typename))
end

test:done(true)
