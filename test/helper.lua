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
-- in the `test_bar`, but this will not always be the case
-- (depends on the order of execution of tests).
-- We prohibit a direct call to `box.cfg` by overriding the
-- `__call` meta method.
box.cfg = setmetatable({}, {__call = function()
    error('A direct call to `box.cfg` is not recommended'..
        ' (you are changing the test runner instance)', 0) end})
