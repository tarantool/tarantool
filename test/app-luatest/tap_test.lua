-- The test for the Test Anything Protocol module.
--
-- Disable stack traces for this test because Tarantool test
-- system also checks test output.

local fio = require('fio')
local justrun = require('luatest.justrun')
local treegen = require('luatest.treegen')
local t = require('luatest')

local tap_module_output_tests = {
    {
        test = [[test:ok(true)]],
        pattern = 'ok - nil',
        strict = false,
    },
    {
        test = [[test:ok(true, 'test desc is ok')]],
        pattern = 'ok - test desc is ok',
        strict = false,
    },
    {
        test = [[test:ok(false)]],
        pattern = 'not ok - nil',
        strict = false,
    },
    {
        test = [[test:ok(false, 'ok', { details = 'joke', state = 'state'})]],
        pattern = '  details: joke',
        strict = false,
    },
    {
        test = [[test:ok(false, 'test desc is ok')]],
        pattern = 'not ok - test desc is ok',
        strict = false,
    },
    {
        test = [[test:fail('failed test')]],
        pattern = 'not ok - failed test',
        strict = false,
    },
    {
        test = [[test:skip('skipped test')]],
        pattern = 'ok - skipped test # skip',
        strict = false,
    },
    {
        test = [[test:diag('diag message')]],
        pattern = '# diag message',
        strict = false,
    },
    {
        test = [[test:is_deeply({a = 1}, {a = 1}, 'tables are equal')]],
        pattern = 'ok - tables are equal',
        strict = false,
    },
    {
        test = [[test:is_deeply({a = 1}, {b = 1}, 'tables are not equal')]],
        pattern = 'not ok - tables are not equal',
        strict = false,
    },
    {
        test = [[test:is_deeply({a = 1}, {b = 1}, 'tables are not equal')]],
        pattern = '  got: 1',
        strict = false,
    },
    {
        test = [[test:is(tonumber('48'), 48, 'tonumber(48) is 48')]],
        pattern = 'ok - tonumber(48) is 48',
        strict = false,
    },
    {
        test = [[test:isnt(0xff, 64, '0xff is not 64')]],
        pattern = 'ok - 0xff is not 64',
        strict = false,
    },
    {
        test = [[test:isnt(1, 1, '1 is not 1')]],
        pattern = 'not ok - 1 is not 1',
        strict = false,
    },
    {
        test = [[test:isnt(1, 1, '1 is not 1')]],
        pattern = '  got: 1',
        strict = false,
    },
    {
        test = [[test:isnil(nil, 'nil is nil')]],
        pattern = 'ok - nil is nil',
        strict = false,
    },
    {
        test = [[test:isnil(48, '48 is nil')]],
        pattern = 'not ok - 48 is nil',
        strict = false,
    },
    {
        test = [[test:isnil(48, '48 is nil')]],
        pattern = '  got: 48',
        strict = false,
    },
    {
        test = [[test:isnumber(10, '10 is a number')]],
        pattern = 'ok - 10 is a number',
        strict = false,
    },
    {
        test = [[test:isnumber(0, '0 is also a number')]],
        pattern = 'ok - 0 is also a number',
        strict = false,
    },
    {
        test = [[test:isnumber('0')]],
        pattern = '  got: string',
        strict = false,
    },
    {
        test = [[test:isstring("blabla", '"blabla" is string')]],
        pattern = 'ok - \"blabla\" is string',
        strict = false,
    },
    {
        test = [[test:isstring(48, '48 is string')]],
        pattern = 'not ok - 48 is string',
        strict = false,
    },
    {
        test = [[test:isstring(48)]],
        pattern = '  got: number',
        strict = false,
    },
    {
        test = [[test:isstring(nil, 'nil is string')]],
        pattern = 'not ok - nil is string',
        strict = false,
    },
    {
        test = [[test:isboolean(true, 'true is boolean')]],
        pattern = 'ok - true is boolean',
        strict = false,
    },
    {
        test = [[test:isboolean(1, '1 is boolean')]],
        pattern = 'not ok - 1 is boolean',
        strict = false,
    },
    {
        test = [[test:isboolean(1)]],
        pattern = '  got: number',
        strict = false,
    },
    {
        test = [[test:istable({}, '{} is a table')]],
        pattern = 'ok - {} is a table',
        strict = false,
    },
    {
        test = [[test:isudata(nil, 'fiber', 'udata')]],
        pattern = 'not ok - udata',
        strict = false,
    },
    {
        test = [[test:isudata(nil, 'fiber', 'udata')]],
        pattern = '  got: nil',
        strict = false,
    },
    {
        test = [[test:isudata(require('fiber').self(), 'some utype', 'udata')]],
        pattern = 'not ok - udata',
        strict = false,
    },
    {
        test = [[test:isudata(require('fiber').self(), 'fiber', 'udata')]],
        pattern = 'ok - udata',
    },
    {
        test = [[test:iscdata(require('ffi').new('int', 10), 'int', 'cdata')]],
        pattern = 'ok - cdata',
        strict = false,
    },
    {
        test = [[test:iscdata('xx', 'int', 'is not cdata')]],
        pattern = 'not ok - is not cdata',
        strict = false,
    },
    {
        test = [[test:iscdata('xx', 'int', 'is not cdata')]],
        pattern = '  got: string',
        strict = false,
    },
    {
        test = [[test:like('abcde', 'cd', 'like(abcde, cd)')]],
        pattern = 'ok - like(abcde, cd)',
        strict = false,
    },
    {
        test = [[test:unlike('abcde', 'acd', 'unlike(abcde, acd)')]],
        pattern = 'ok - unlike(abcde, acd)',
        strict = false,
    },
    -- gh-4125: strict nulls comparisons.
    {
        test = [[test:is(box.NULL, nil, "box.NULL == nil")]],
        pattern = 'not ok - box.NULL == nil',
        strict = true,
    },
    {
        test = [[test:is(nil, box.NULL, "nil == box.NULL")]],
        pattern = 'not ok - nil == box.NULL',
        strict = true,
    },
    {
        test = [[test:is(nil, box.NULL, "nil == box.NULL")]],
        pattern = '  expected: null',
        strict = true,
    },
    {
        test = [[test:is(box.NULL, box.NULL, "box.NULL == box.NULL")]],
        pattern = 'ok - box.NULL == box.NULL',
        strict = true,
    },
    {
        test = [[test:is(nil, nil, "nil == nil")]],
        pattern = 'ok - nil == nil',
        strict = true,
    },
    {
        test = [[test:isnt(box.NULL, nil, "box.NULL != nil")]],
        pattern = 'ok - box.NULL != nil',
        strict = true,
    },
    {
        test = [[test:isnt(nil, box.NULL, "nil != box.NULL")]],
        pattern = 'ok - nil != box.NULL',
        strict = true,
    },
    {
        test = [[test:isnt(box.NULL, box.NULL, "box.NULL != box.NULL")]],
        pattern = 'not ok - box.NULL != box.NULL',
        strict = true,
    },
    {
        test = [[test:isnt(nil, nil, "nil != nil strict = true")]],
        pattern = 'not ok - nil != nil strict = true',
        strict = true,
    },
}

local pg = t.group('tap_output', tap_module_output_tests)

pg.test_tap_module_output = function(pg)
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'tap_test_tap_module_output.lua'
    local test_template = [[
local tap = require('tap')
local test = tap.test('test name')
test.trace = false
test.strict = %s
test:plan(1)
%s
]]
    local chunk = (test_template):format(pg.params.strict, pg.params.test)
    treegen.write_file(dir, script_name, chunk)
    local opts = {nojson = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_str_contains(res.stdout, 'TAP version 13')
    t.assert_str_contains(res.stdout, '1..1')
    t.assert_str_contains(res.stdout, pg.params.pattern)
end

local is_deeply_tests = {
    {
        got = {1, 2, 3},
        expected = '200',
        strict = false,
        res_bool = false,
    },
    {
        got = '200',
        expected = {1, 2, 3},
        strict = false,
        res_bool = false,
    },
    {
        got = 1,
        expected = 1,
        strict = false,
        res_bool = true,
    },
    {
        got = 'abc',
        expected = 'abc',
        strict = false,
        res_bool = true,
    },
    {
        got = {},
        expected = {},
        strict = false,
        res_bool = true,
    },
    {
        got = {1},
        expected = {1},
        strict = false,
        res_bool = true,
    },
    {
        got = {1},
        expected = {2},
        strict = false,
        res_bool = false,
    },
    {
        got = {1, 2, { 3, 4 }},
        expected = {1, 2, { 3, 5 }},
        strict = false,
        res_bool = false,
    },
    -- gh-4125: `is_deeply` inconsistently works with `box.NULL`.
    {
        got = {},
        expected = {a = box.NULL},
        strict = false,
        res_bool = true,
    },
    {
        got = {a = box.NULL},
        expected = {},
        strict = false,
        res_bool = true,
    },
    {
        got = {a = box.NULL},
        expected = {b = box.NULL},
        strict = false,
        res_bool = true,
    },
    {
        got = {a = box.NULL},
        expected = {b = box.NULL, c = box.NULL},
        strict = false,
        res_bool = true,
    },
    {
        got = nil,
        expected = box.NULL,
        strict = false,
        res_bool = true,
    },
    {
        got = box.NULL,
        expected = nil,
        strict = false,
        res_bool = true,
    },
    {
        got = {a = box.NULL},
        expected = {a = box.NULL},
        strict = false,
        res_bool = true,
    },
    {
        got = {},
        expected = {a = box.NULL},
        strict = true,
        res_bool = false,
    },
    {
        got = {a = box.NULL},
        expected = {},
        strict = true,
        res_bool = false,
    },
    {
        got = {a = box.NULL},
        expected = {b = box.NULL},
        strict = true,
        res_bool = false,
    },
    {
        got = {a = box.NULL},
        expected = {b = box.NULL, c = box.NULL},
        strict = true,
        res_bool = false,
    },
    {
        got = nil,
        expected = box.NULL,
        strict = true,
        res_bool = false,
    },
    {
        got = box.NULL,
        expected = nil,
        strict = true,
        res_bool = false,
    },
    {
        got = {a = box.NULL},
        expected = {a = box.NULL},
        strict = true,
        res_bool = true,
    },
}

local pg_is_deeply = t.group('tap_is_deeply', is_deeply_tests)

pg_is_deeply.before_test('test_tap_is_deeply', function(pg)
    -- We are not interested in a TAP output, suppress TAP output,
    -- otherwise it confuses test-run.py.
    local dev_null = '/dev/null'
    t.assert_equals(fio.path.exists(dev_null), true)
    pg.io_output = io.output()
    io.output(dev_null)
end)

pg_is_deeply.after_test('test_tap_is_deeply', function(pg)
    io.output(pg.io_output)
end)

pg_is_deeply.test_tap_is_deeply = function(pg)
    local tap = require('tap')
    local test = tap.test('test_tap_is_deeply')
    test.trace = false
    test.strict = pg.params.strict
    test:plan(1)
    local res = test:is_deeply(pg.params.got, pg.params.expected)
    t.assert_equals(res, pg.params.res_bool)
end

local pg_strict = t.group('tap_strict_flag_inheritance')

pg_strict.before_test('test_strict_flag_inheritance', function(pg)
    -- We are not interested in a TAP output, suppress TAP output,
    -- otherwise it confuses test-run.py.
    local dev_null = '/dev/null'
    t.assert_equals(fio.path.exists(dev_null), true)
    pg.io_output = io.output()
    io.output(dev_null)
end)

pg_strict.after_test('test_strict_flag_inheritance', function(pg)
    io.output(pg.io_output)
end)

pg_strict.test_strict_flag_inheritance = function()
    local tap = require('tap')
    local test = tap.test('test_strict_flag_inheritance')
    test.trace = false
    test.strict = true
    test:plan(1)
    test:test('check strict flag inheritance', function(tc)
        tc:plan(2)
        t.assert_equals(tc:is_deeply({}, {a = box.NULL}), false)
        t.assert_equals(tc:is_deeply(nil, box.NULL), false)
    end)
    test.strict = false
end

local iscdata_tests = {
    {
        value = 'xx',
        ctype = 'int',
        res_bool = false,
    },
    {
        value = 10,
        ctype = 'int',
        res_bool = false,
    },
    {
        value = require('ffi').new('int', 10),
        ctype = 'int',
        res_bool = true,
    },
    {
        value = require('ffi').new('unsigned int', 10),
        ctype = 'int',
        res_bool = false,
    },
}

local pg_iscdata = t.group('tap_iscdata', iscdata_tests)

pg_iscdata.before_test('test_tap_iscdata', function(pg)
    -- We are not interested in a TAP output, suppress TAP output,
    -- otherwise it confuses test-run.py.
    local dev_null = '/dev/null'
    t.assert_equals(fio.path.exists(dev_null), true)
    pg.io_output = io.output()
    io.output(dev_null)
end)

pg_iscdata.after_test('test_tap_iscdata', function(pg)
    io.output(pg.io_output)
end)

pg_iscdata.test_tap_iscdata = function(pg)
    local tap = require('tap')
    local test = tap.test('test_tap_iscdata')
    test.trace = false
    test:plan(1)
    local res = test:iscdata(pg.params.value, pg.params.ctype)
    t.assert_equals(res, pg.params.res_bool)
end

local g = t.group('tap')

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
test:test('subtest 1', function(t)
    t:plan(2)
    t:ok(true, 'true')
    t:ok(true, 'true')
    -- test:check() is called automatically
end)
    ]])
    local opts = {nojson = true}
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
local sub2 = test:test('subtest 2')
sub2:plan(1)
sub2:ok(true, 'true in subtest')
sub2:diag('hello from subtest')
sub2:check() -- Please call `check()` explicitly.
    ]])
    local opts = {nojson = true}
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_str_contains(res.stdout, 'ok - true in subtest')
    t.assert_str_contains(res.stdout, '# hello from subtest')
    t.assert_str_contains(res.stdout, 'ok - subtest 2')
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
    local opts = {nojson = true}
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
test:test('bad plan', function(t)
    t:plan(3)
    t:ok(true, 'true')
end)
    ]])
    local opts = {nojson = true}
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
test:test('failed subtest', function(t)
    t:plan(1)
    t:fail('failed subtest')
end)
    ]])
    local opts = {nojson = true}
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
