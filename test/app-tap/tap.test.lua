#!/usr/bin/env tarantool

--
-- Test suite for The Test Anything Protocol module implemented
-- using module itself.
--

-- Load 'tap' module
local tap = require "tap"

--
-- Create a root test
--
test = tap.test("root test")
-- Disable stack traces for this test because Tarantool test system also
-- checks test output.
test.trace = false

--
-- ok, fail and skip predicates
--

test:plan(32) -- plan to run 3 test
test:ok(true, 'true') -- basic function
local extra = { state = 'some userful information to debug on failure',
        details = 'a table argument formatted using yaml.encode()' }
test:ok(true, "extra information is not printed on success", extra)
test:ok(false, "extra printed using yaml only on failure", extra)

test:fail('failed') -- always fail the test
test:skip('test marked as ok and skipped')

--
-- is and isnt predicates
--
test:is(tonumber("48"), 48, "tonumber(48) is 48")
test:isnt(0xff, 64, "0xff is not 64")
test:isnt(1, 1, "1 is not 1")

--
-- type predicates
--
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

--
-- Any test also can create unlimited number of sub tests.
-- Subtest with callbacks (preferred).
--
test:test("subtest 1", function(t)
    t:plan(2)
    t:ok(true, 'true')
    t:ok(true, 'true')
    -- test:check() is called automatically
end)
-- each subtest is counted in parent

--
-- Subtest without callbacks.
--
sub2 = test:test("subtest 2")
    sub2:plan(1)
    sub2:ok(true, 'true in subtest')
    sub2:diag('hello from subtest')
    sub2:check() -- please call check() explicitly

--
-- Multisubtest
--
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

---
--- Subtest with bad plan()
---
test:test("bad plan", function(t)
    t:plan(3)
    t:ok(true, 'true')
end)


test:test("failed subtest", function(t)
    t:plan(1)
    t:fail("failed subtest")
end)



test:test('is_deeply', function(t)
    t:plan(6)

    t:is_deeply(1, 1, '1 and 1')
    t:is_deeply('abc', 'abc', 'abc and abc')
    t:is_deeply({}, {}, 'empty tables')
    t:is_deeply({1}, {1}, '{1} and {1}')
    t:is_deeply({1}, {2}, '{1} and {2}')
    t:is_deeply({1, 2, { 3, 4 }}, {1, 2, { 3, 5 }}, '{1,2,{3,4}} and {1,2,{3,5}}')

end)


test:test('like', function(t)
    t:plan(2)
    t:like('abcde', 'cd', 'like(abcde, cd)')
    t:unlike('abcde', 'acd', 'unlike(abcde, acd)')
end)

--
-- Finish root test. Since we used non-callback variant, we have to
-- call check explicitly.
--
test:check() -- call check() explicitly
os.exit(0)
