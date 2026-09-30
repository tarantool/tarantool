-- There are situations when the Tarantool instance is configured
-- for testing. For example:
--
--     g.test_foo = function()
--         box.cfg{params}
--         ...
--         t.assert_equals(foo, bar)
--
--     g.test_bar = function()
--         t.assert_equals(foo, bar)
--
-- We expect that the test environment will not be configured
-- in the `test_bar` test, but this will not always be the case
-- (depends on the order of execution of tests).
-- We prohibit a direct call to `box.cfg` by overriding the
-- `__call` meta method. Motivating issue tarantool/luatest#245.
box.cfg = setmetatable({}, {__call = function()
    error('A direct call to `box.cfg` is forbidden' ..
          ' (you are changing the test runner instance)', 0)
end})

-- Tests start tarantool processes all the time, and a few of them use
-- overrides of built-in modules, whose lookup takes ~16 ms of every start
-- (x86_64, RelWithDebInfo). Disable the lookup in the processes that tests
-- start, unless it is set explicitly: the tests that use overrides enable
-- it back.
if os.getenv('TT_OVERRIDE_BUILTIN') == nil then
    os.setenv('TT_OVERRIDE_BUILTIN', 'false')
end
