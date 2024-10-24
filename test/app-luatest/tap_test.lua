-- The test for The Test Anything Protocol module.
--
-- Disable stack traces for this test because Tarantool test
-- system also checks test output.

local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

-- The testcase tests `ok`, `fail` and `skip` predicates.
g.test_tap_module_test_statuses = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_test_statuses.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(5)
test:ok(true, 'true') -- basic function
local extra = { state = 'some useful information to debug on failure',
        details = 'a table argument formatted using yaml.encode()' }
test:ok(true, "extra information is not printed on success", extra)
test:ok(false, "extra printed using yaml only on failure", extra)

test:fail('failed') -- always fail the test
test:skip('test marked as ok and skipped')
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..5
ok - true
ok - extra information is not printed on success
not ok - extra printed using yaml only on failure
  ---
  state: some useful information to debug on failure
  details: a table argument formatted using yaml.encode()
  ...
not ok - failed
ok - test marked as ok and skipped # skip]])
end

-- The testcase tests `is` and `isnt` predicates.
g.test_tap_module_is_and_isnt = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_is_and_isnt.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(3)
test:is(tonumber('48'), 48, 'tonumber(48) is 48')
test:isnt(0xff, 64, '0xff is not 64')
test:isnt(1, 1, '1 is not 1')
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..3
ok - tonumber(48) is 48
ok - 0xff is not 64
not ok - 1 is not 1
  ---
  unexpected: 1
  got: 1
  ...]])
end

-- The testcase tests type predicates.
g.test_tap_module_type_predicates = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_type_predicates.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(18)
test:isnil(nil, 'nil is nil')
test:isnil(48, '48 is nil')
test:isnumber(10, '10 is a number')
test:isnumber(0, '0 is also a number')
test:isstring("blabla", '"blabla" is string')
test:isstring(48, '48 is string')
test:isstring(nil, 'nil is string')
test:isboolean(true, 'true is boolean')
test:isboolean(1, '1 is boolean')
test:istable({}, '{} is a table')
local udata = require('fiber').self()
test:isudata(nil, 'fiber', 'udata')
test:isudata(udata, 'some utype', 'udata')
test:isudata(udata, 'fiber', 'udata')
local ffi = require('ffi')
test:iscdata('xx', 'int', 'cdata type')
test:iscdata(10, 'int', 'cdata type')
test:iscdata(ffi.new('int', 10), 'int', 'cdata type')
test:iscdata(ffi.new('unsigned int', 10), 'int', 'cdata type')
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..18
ok - nil is nil
not ok - 48 is nil
  ---
  expected: nil
  got: 48
  ...
ok - 10 is a number
ok - 0 is also a number
ok - "blabla" is string
not ok - 48 is string
  ---
  expected: string
  got: number
  ...
not ok - nil is string
  ---
  expected: string
  got: nil
  ...
ok - true is boolean
not ok - 1 is boolean
  ---
  expected: boolean
  got: number
  ...
ok - {} is a table
not ok - udata
  ---
  expected: userdata<fiber>
  got: nil
  ...
not ok - udata
  ---
  expected: userdata<some utype>
  got: userdata<fiber>
  ...
ok - udata
not ok - cdata type
  ---
  expected: ctype<int>
  got: string
  ...
not ok - cdata type
  ---
  expected: ctype<int>
  got: number
  ...
ok - cdata type
not ok - cdata type
  ---
  expected: ctype<int>
  got: ctype<unsigned int>
  ...]])
end

-- gh-4125: Strict nulls comparisons.
g.test_tap_module_strict_null_comparisons = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_strict_null_comparisons.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(8)
test.strict = true
test:is(box.NULL, nil, "box.NULL == nil strict = true")
test:is(nil, box.NULL, "nil == box.NULL strict = true")
test:is(box.NULL, box.NULL, "box.NULL == box.NULL strict = true")
test:is(nil, nil, "nil == nil strict = true")
test:isnt(box.NULL, nil, "box.NULL != nil strict = true")
test:isnt(nil, box.NULL, "nil != box.NULL strict = true")
test:isnt(box.NULL, box.NULL, "box.NULL != box.NULL strict = true")
test:isnt(nil, nil, "nil != nil strict = true")
test.strict = false
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..8
not ok - box.NULL == nil strict = true
  ---
  got: null
  ...
not ok - nil == box.NULL strict = true
  ---
  expected: null
  ...
ok - box.NULL == box.NULL strict = true
ok - nil == nil strict = true
ok - box.NULL != nil strict = true
ok - nil != box.NULL strict = true
not ok - box.NULL != box.NULL strict = true
  ---
  unexpected: null
  got: null
  ...
not ok - nil != nil strict = true]])
end

-- Any test also can create unlimited number of sub tests.
-- Subtest with callbacks (preferred).
-- Each subtest is counted in parent.
g.test_tap_module_subtests_with_callbacks = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_subtests_with_callbacks.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(8)
test:test("subtest 1", function(t)
    t:plan(2)
    t:ok(true, 'true')
    t:ok(true, 'true')
    -- test:check() is called automatically
end)
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..8
    # subtest 1
    1..2
    ok - true
    ok - true
    # subtest 1: end
ok - subtest 1]])
end

-- The testcase tests a TAP subtest without callbacks.
g.test_tap_module_subtests_without_callbacks = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_subtests_without_callbacks.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(8)
local sub2 = test:test("subtest 2")
    sub2:plan(1)
    sub2:ok(true, 'true in subtest')
    sub2:diag('hello from subtest')
    sub2:check() -- please call check() explicitly
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..8
    1..1
    ok - true in subtest
    # hello from subtest
ok - subtest 2]])
end

-- The testcase tests a TAP multisubtest.
g.test_tap_module_multisubtest = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_multisubtest.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(8)
test:test("1 level", function(t)
    t:plan(1)
    t:test("2 level", function(t)
        t:plan(1)
        t:test("3 level", function(t)
            t:plan(1)
            t:test("4 level", function(t)
                t:plan(1)
                t:test("5 level", function(t)
                    t:plan(1)
                    t:ok(true, 'ok')
                end)
            end)
        end)
    end)
end)
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..8
    # 1 level
    1..1
        # 2 level
        1..1
            # 3 level
            1..1
                # 4 level
                1..1
                    # 5 level
                    1..1
                    ok - ok
                    # 5 level: end
                ok - 5 level
                # 4 level: end
            ok - 4 level
            # 3 level: end
        ok - 3 level
        # 2 level: end
    ok - 2 level
    # 1 level: end
ok - 1 level]])
end

-- The testcase tests a TAP subtest with a bad plan.
g.test_tap_module_bad_plan = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_bad_plan.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(8)
test:test("bad plan", function(t)
    t:plan(3)
    t:ok(true, 'true')
end)
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..8
    # bad plan
    1..3
    ok - true
    # bad plan: end
not ok - bad plan
  ---
  planned: 3
  run: 1
  ...]])
end

g.test_tap_module_failed_subtest = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_failed_subtest.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(8)
test:test("failed subtest", function(t)
    t:plan(1)
    t:fail("failed subtest")
end)
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..8
    # failed subtest
    1..1
    not ok - failed subtest
    # failed subtest: end
not ok - failed subtests
  ---
  planned: 1
  failed: 1
  ...]])
end

g.test_tap_module_is_deeply = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_is_deeply.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(8)
test:test('is_deeply', function(t)
    t:plan(21)

    t:is_deeply(1, 1, '1 and 1')
    t:is_deeply('abc', 'abc', 'abc and abc')
    t:is_deeply({}, {}, 'empty tables')
    t:is_deeply({1}, {1}, '{1} and {1}')
    t:is_deeply({1}, {2}, '{1} and {2}')
    t:is_deeply({1, 2, { 3, 4 }}, {1, 2, { 3, 5 }},
                '{1,2,{3,4}} and {1,2,{3,5}}')

    --
    -- gh-4125: is_deeply inconsistently works with box.NULL.
    --
    t:is_deeply({}, {a = box.NULL}, '{} and {a = box.NULL} strict = false')
    t:is_deeply({a = box.NULL}, {}, '{a = box.NULL} and {} strict = false')
    t:is_deeply({a = box.NULL}, {b = box.NULL},
                '{a = box.NULL} and {b = box.NULL} strict = false')
    t:is_deeply({a = box.NULL}, {b = box.NULL, c = box.NULL},
                '{a = box.NULL} and {b = box.NULL, c = box.NULL}\
                 strict = false')
    t:is_deeply(nil, box.NULL, 'nil and box.NULL strict = false')
    t:is_deeply(box.NULL, nil, 'box.NULL and nil strict = false')
    t:is_deeply({a = box.NULL}, {a = box.NULL},
                '{a = box.NULL} and {a = box.NULL} strict false')

    t.strict = true
    t:is_deeply({}, {a = box.NULL}, '{} and {a = box.NULL} strict = true')
    t:is_deeply({a = box.NULL}, {}, '{a = box.NULL} and {} strict = true')
    t:is_deeply({a = box.NULL}, {b = box.NULL},
                '{a = box.NULL} and {b = box.NULL} strict = true')
    t:is_deeply({a = box.NULL}, {b = box.NULL, c = box.NULL},
                '{a = box.NULL} and {b = box.NULL, c = box.NULL} strict = true')
    t:is_deeply(nil, box.NULL, 'nil and box.NULL strict = true')
    t:is_deeply(box.NULL, nil, 'box.NULL and nil strict = true')
    t:is_deeply({a = box.NULL}, {a = box.NULL},
                '{a = box.NULL} and {a = box.NULL} strict true')

    t:test('check strict flag inheritance', function(t)
        t:plan(2)
        t:is_deeply({}, {a = box.NULL}, '{} and {a = box.NULL} strict = true')
        t:is_deeply(nil, box.NULL, 'nil and box.NULL strict = true')
    end)

    t.strict = false
end)
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..8
    # is_deeply
    1..21
    ok - 1 and 1
    ok - abc and abc
    ok - empty tables
    ok - {1} and {1}
    not ok - {1} and {2}
      ---
      path: //1
      strict: false
      expected: 2
      got: 1
      ...
    not ok - {1,2,{3,4}} and {1,2,{3,5}}
      ---
      path: //3/2
      strict: false
      expected: 5
      got: 4
      ...
    ok - {} and {a = box.NULL} strict = false
    ok - {a = box.NULL} and {} strict = false
    ok - {a = box.NULL} and {b = box.NULL} strict = false
    ok - {a = box.NULL} and {b = box.NULL, c = box.NULL}
                 strict = false
    ok - nil and box.NULL strict = false
    ok - box.NULL and nil strict = false
    ok - {a = box.NULL} and {a = box.NULL} strict false
    not ok - {} and {a = box.NULL} strict = true
      ---
      strict: true
      expected: key a
      got: nil
      ...
    not ok - {a = box.NULL} and {} strict = true
      ---
      path: //a
      strict: true
      expected: nil
      got: cdata
      ...
    not ok - {a = box.NULL} and {b = box.NULL} strict = true
      ---
      path: //a
      strict: true
      expected: nil
      got: cdata
      ...
    not ok - {a = box.NULL} and {b = box.NULL, c = box.NULL} strict = true
      ---
      path: //a
      strict: true
      expected: nil
      got: cdata
      ...
    not ok - nil and box.NULL strict = true
      ---
      got: nil
      expected: cdata
      strict: true
      ...
    not ok - box.NULL and nil strict = true
      ---
      got: cdata
      expected: nil
      strict: true
      ...
    ok - {a = box.NULL} and {a = box.NULL} strict true
        # check strict flag inheritance
        1..2
        not ok - {} and {a = box.NULL} strict = true
          ---
          strict: true
          expected: key a
          got: nil
          ...
        not ok - nil and box.NULL strict = true
          ---
          got: nil
          expected: cdata
          strict: true
          ...
        # check strict flag inheritance: end
    not ok - failed subtests
      ---
      planned: 2
      failed: 2
      ...
    # is_deeply: end
not ok - failed subtests
  ---
  planned: 21
  failed: 9
  ...]])
end

g.test_tap_module_like_keyword = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_like_keyword.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(8)
test:test('like', function(t)
    t:plan(2)
    t:like('abcde', 'cd', 'like(abcde, cd)')
    t:unlike('abcde', 'acd', 'unlike(abcde, acd)')
end)
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..8
    # like
    1..2
    ok - like(abcde, cd)
    ok - unlike(abcde, acd)
    # like: end
ok - like]])
end

-- Test, that in case of not strict comparison the order of
-- arguments does not matter.
g.test_tap_module_comparison_order_does_not_matter = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name =
        'tap_test_tap_module_comparison_order_does_not_matter.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(2)
test:is_deeply({1, 2, 3}, '200', "compare {1, 2, 3} and '200'")
test:is_deeply('200', {1, 2, 3}, "compare '200' and {1, 2, 3}")
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..2
not ok - compare {1, 2, 3} and '200'
  ---
  strict: false
  expected: '200'
  got:
  - 1
  - 2
  - 3
  ...
not ok - compare '200' and {1, 2, 3}
  ---
  strict: false
  expected:
  - 1
  - 2
  - 3
  got: '200'
  ...]])
end

-- Finish root test. Since we used non-callback variant, we have
-- to call check explicitly.
g.test_tap_module_check = function()
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_check.lua'
    treegen.write_file(dir, script_name, [[
local tap = require('tap')
local test = tap.test('root test')
test.trace = false
test:plan(2)
test:ok(true)
test:ok(true)
test:check() -- call check() explicitly.
    ]])
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, [[
TAP version 13
1..2
ok - nil
ok - nil]])
end
