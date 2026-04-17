local server = require('luatest.server')
local t = require('luatest')

local g = t.group("integer_overflow", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_each(function(cg)
    cg.server:drop()
end)

--
-- gh-3735: make sure that integer overflows errors are
-- handled during VDBE execution.
--
g.test_3735_integer_overflow = function(cg)
    cg.server:exec(function()
        local sql = 'SELECT (2147483647 * 2147483647 * 2147483647);'
        local _, err = box.execute(sql)
        local exp_err = "Failed to execute SQL statement: integer is overflowed"
        t.assert_equals(err.message, exp_err)
        local res = box.execute('SELECT (-9223372036854775808 / -1);')
        t.assert_equals(res.rows, {{9223372036854775808ULL}})
        _, err = box.execute('SELECT (-9223372036854775808 - 1);')
        t.assert_equals(err.message, exp_err)

        -- Literals are checked right after parsing.
        _, err = box.execute('SELECT -9223372036854775809;')
        exp_err = "Integer literal -9223372036854775809 exceeds the " ..
                  "supported range [-9223372036854775808, 18446744073709551615]"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute('SELECT 18446744073709551616 - 1;')
        exp_err = "Integer literal 18446744073709551616 exceeds the " ..
                  "supported range [-9223372036854775808, 18446744073709551615]"
        t.assert_equals(err.message, exp_err)
        res = box.execute('SELECT 18446744073709551615;')
        t.assert_equals(res.rows, {{18446744073709551615ULL}})
        _, err = box.execute('SELECT 18446744073709551616;')
        exp_err = "Integer literal 18446744073709551616 exceeds the " ..
                  "supported range [-9223372036854775808, 18446744073709551615]"
        t.assert_equals(err.message, exp_err)
    end)
end

--
-- gh-3810: range of integer is extended up to 2^64 - 1.
--
g.test_3810_extended_integer = function(cg)
    cg.server:exec(function()
        local sql = 'SELECT (9223372036854775807 + 9223372036854775807 + 2);'
        local _, err = box.execute(sql)
        local exp_err = "Failed to execute SQL statement: integer is overflowed"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute('SELECT 18446744073709551615 * 2;')
        t.assert_equals(err.message, exp_err)
        local res = box.execute('SELECT (-9223372036854775807 * (-2));')
        t.assert_equals(res.rows, {{18446744073709551614ULL}})
    end)
end

--
-- Make sure CAST may also leads to overflow.
--
g.test_cast_overflow = function(cg)
    cg.server:exec(function ()
        local sql = "SELECT CAST('18446744073709551616' AS INTEGER);"
        local _, err = box.execute(sql)
        local exp_err = "Type mismatch: can not convert " ..
                        "string('18446744073709551616') to integer"
        t.assert_equals(err.message, exp_err)

        -- Due to inexact representation of large integers in terms of
        -- floating point numbers, numerics with value < UINT64_MAX
        -- have UINT64_MAX + 1 value in integer representation:
        -- float 18446744073709551600 -> int (18446744073709551616),
        -- with error due to conversion = 16.
        _, err = box.execute('SELECT CAST(18446744073709551600e0 AS INTEGER);')
        exp_err = "Type mismatch: can not convert " ..
                  "double(1.84467440737096e+19) to integer"
        t.assert_equals(err.message, exp_err)
    end)
end

--
-- gh-3810: make sure that if space contains integers in range
-- [INT64_MAX, UINT64_MAX], they are handled inside SQL in a
-- proper way, which now means that an error is raised.
--
g.test_3810_integers_in_range = function(cg)
    cg.server:exec(function()
        box.execute('CREATE TABLE t (id INT PRIMARY KEY);')
        local res = box.space.t:insert({18446744073709551615ULL})
        t.assert_equals(res, {18446744073709551615ULL})
        res = box.execute('SELECT * FROM t;')
        t.assert_equals(res.rows, {{18446744073709551615ULL}})
        box.space.t:drop()
    end)
end
