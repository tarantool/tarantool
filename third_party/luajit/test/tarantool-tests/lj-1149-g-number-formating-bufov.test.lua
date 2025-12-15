local tap = require('tap')

-- Test file to demonstrate stack-buffer-overflow in the
-- `lj_strfmt_wfnum()` call.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1149.

local test = tap.test('lj-1149-g-number-formating-bufov')
test:plan(1)

-- XXX: The test shows stack-buffer-overflow only under ASAN.
-- The number value for the test has the same precision
-- (`prec` = 5) and amount of digits (`hilen` = 5) for the decimal
-- representation. Hence, with `ndhi` == 0, the `ndlo` part
-- becomes 64 (the size of the `nd` stack buffer), and the
-- overflow occurs.
-- See details in the <src/lj_strfmt_num.c>:`lj_strfmt_wfnum()`.
test:is(string.format('%7g', 0x1.144399609d407p+401), '5.5733e+120',
        'correct format %7g result')

test:done(true)
