#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(25)

--!./tcltestrunner.lua
-- 2010 August 27
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- Testing of function factoring and the sql_DETERMINISTIC flag.
--

-- Verify that constant string expressions that get factored into initializing
-- code are not reused between function parameters and other values in the
-- VDBE program, as the function might have changed the encoding.
--
test:do_execsql_test(
    "func5-1.1",
    [[
        CREATE TABLE t1(x INT PRIMARY KEY,a TEXT,b TEXT,c INT );
        INSERT INTO t1 VALUES(1,'ab','cd',1);
        INSERT INTO t1 VALUES(2,'gh','ef',5);
        INSERT INTO t1 VALUES(3,'pqr','fuzzy',99);
        INSERT INTO t1 VALUES(4,'abcdefg','xy',22);
        INSERT INTO t1 VALUES(5,'shoe','mayer',2953);
        SELECT x FROM t1 WHERE c=position(b, 'abcdefg') OR a='abcdefg' ORDER BY +x;
    ]], {
        -- <func5-1.1>
        2, 4
        -- </func5-1.1>
    })

test:do_execsql_test(
    "func5-1.2",
    [[
        SELECT x FROM t1 WHERE a='abcdefg' OR c=position(b, 'abcdefg') ORDER BY +x;
    ]], {
        -- <func5-1.1>
        2, 4
        -- </func5-1.1>
    })

-- Verify that sql_DETERMINISTIC functions get factored out of the
-- evaluation loop whereas non-deterministic functions do not.  counter1()
-- is marked as non-deterministic and so is not factored out of the loop,
-- and it really is non-deterministic, returning a different result each
-- time.  But counter2() is marked as deterministic, so it does get factored
-- out of the loop.  counter2() has the same implementation as counter1(),
-- returning a different result on each invocation, but because it is
-- only invoked once outside of the loop, it appears to return the same
-- result multiple times.
--
test:do_execsql_test(
    "func5-2.1",
    [[
        CREATE TABLE t2(x  INT PRIMARY KEY,y INT );
        INSERT INTO t2 VALUES(1,2),(3,4),(5,6),(7,8);
        SELECT x, y FROM t2 WHERE x+5=5+x ORDER BY +x;
    ]], {
        -- <func5-2.1>
        1, 2, 3, 4, 5, 6, 7, 8
        -- </func5-2.1>
    })

_G.global_counter = 0

box.schema.func.create('COUNTER1', {language = 'Lua', is_deterministic = false,
                       param_list = {'any'}, returns = 'integer',
                       exports = {'SQL', 'LUA'},
                       body = [[
                           function(str)
                               global_counter = global_counter + 1
                               return global_counter
                           end
                       ]]})

box.schema.func.create('COUNTER2', {language = 'Lua', is_deterministic = true,
                       param_list = {'any'}, returns = 'integer',
                       exports = {'SQL', 'LUA'},
                       body = [[
                           function(str)
                                   global_counter = global_counter + 1
                                   return global_counter
                               end
                       ]]})

test:do_execsql_test(
    "func5-2.2",
    [[
        SELECT x, y FROM t2 WHERE x+counter1('hello')=counter1('hello')+x ORDER BY +x;
    ]], {
        -- <func5-2.2>
        -- </func5-2.2>
    })

test:do_execsql_test(
    "func5-2.3",
    [[
        SELECT x, y FROM t2 WHERE x+counter2('hello')=counter2('hello')+x ORDER BY +x;
    ]], {
        -- <func5-2.2>
        1, 2, 3, 4, 5, 6, 7, 8
        -- </func5-2.2>
    })

-- The following tests ensures that GREATEST() and LEAST()
-- functions raise error if argument's collations are incompatible.

test:do_catchsql_test(
    "func-5-3.1",
    [[
        SELECT GREATEST('a' COLLATE "unicode", 'A' COLLATE "unicode_ci");
    ]],
    {
        -- <func5-3.1>
        1, "Illegal mix of collations"
        -- </func5-3.1>
    }
)

test:do_catchsql_test(
    "func-5-3.2",
    [[
        CREATE TABLE test1 (s1 VARCHAR(5) PRIMARY KEY COLLATE "unicode");
        CREATE TABLE test2 (s2 VARCHAR(5) PRIMARY KEY COLLATE "unicode_ci");
        INSERT INTO test1 VALUES ('a');
        INSERT INTO test2 VALUES ('a');
        SELECT GREATEST(s1, s2) FROM test1 JOIN test2;
    ]],
    {
        -- <func5-3.2>
        1, "Illegal mix of collations"
        -- </func5-3.2>
    }
)

test:do_catchsql_test(
    "func-5-3.3",
    [[
        SELECT GREATEST ('abc', 'asd' COLLATE "binary", 'abc' COLLATE "unicode")
    ]],
    {
        -- <func5-3.3>
        1, "Illegal mix of collations"
        -- </func5-3.3>
    }
)

test:do_execsql_test(
    "func-5-3.4",
    [[
        SELECT GREATEST (s1, 'asd' COLLATE "binary", s2) FROM test1 JOIN test2;
    ]], {
        -- <func5-3.4>
        "asd"
        -- </func5-3.4>
    }
)

test:do_catchsql_test(
    "func-5.3.5",
    [[
        CREATE TABLE test3 (s3 VARCHAR(5) PRIMARY KEY COLLATE "unicode");
        CREATE TABLE test4 (s4 VARCHAR(5) PRIMARY KEY COLLATE "unicode");
        CREATE TABLE test5 (s5 VARCHAR(5) PRIMARY KEY COLLATE "binary");
        INSERT INTO test3 VALUES ('a');
        INSERT INTO test4 VALUES ('a');
        INSERT INTO test5 VALUES ('a');
        SELECT GREATEST(s3, s4, s5) FROM test3 JOIN test4 JOIN test5;
    ]],
    {
        -- <func5-3.5>
        1, "Illegal mix of collations"
        -- </func5-3.5>
    }
)

test:do_catchsql_test(
    "func-5-3.6",
    [[
        SELECT LEAST('a' COLLATE "unicode", 'A' COLLATE "unicode_ci");
    ]],
    {
        -- <func5-3.6>
        1, "Illegal mix of collations"
        -- </func5-3.6>
    }
)

test:do_catchsql_test(
    "func-5-3.7",
    [[
        SELECT LEAST(s1, s2) FROM test1 JOIN test2;
    ]],
    {
        -- <func5-3.7>
        1, "Illegal mix of collations"
        -- </func5-3.7>
    }
)

test:do_catchsql_test(
    "func-5-3.8",
    [[
        SELECT LEAST('abc', 'asd' COLLATE "binary", 'abc' COLLATE "unicode")
    ]],
    {
        -- <func5-3.8>
        1, "Illegal mix of collations"
        -- </func5-3.8>
    }
)

test:do_execsql_test(
    "func-5-3.9",
    [[
        SELECT LEAST(s1, 'asd' COLLATE "binary", s2) FROM test1 JOIN test2;
    ]], {
        -- <func5-3.9>
        "a"
        -- </func5-3.9>
    }
)

test:do_catchsql_test(
    "func-5.3.10",
    [[
        SELECT LEAST(s3, s4, s5) FROM test3 JOIN test4 JOIN test5;
    ]],
    {
        -- <func5-3.10>
        1, "Illegal mix of collations"
        -- <func5-3.10>
    }
)

-- Order of arguments of LEAST/GREATEST functions doesn't affect
-- the result: boolean is always less than numbers, which
-- are less than strings.
--
test:do_execsql_test(
    "func-5-4.1",
    [[
        SELECT GREATEST (false, 'STR', 1, 0.5);
    ]], { "STR" } )

test:do_execsql_test(
    "func-5-4.2",
    [[
        SELECT GREATEST ('STR', 1, 0.5, false);
    ]], { "STR" } )

test:do_execsql_test(
    "func-5-4.3",
    [[
        SELECT LEAST('STR', 1, 0.5, false);
    ]], { false } )

test:do_execsql_test(
    "func-5-4.4",
    [[
        SELECT LEAST(false, 'STR', 1, 0.5);
    ]], { false } )

-- gh-4453: GREATEST()/LEAST() require at least two arguments
-- be passed to these functions.
--
test:do_catchsql_test(
    "func-5-5.1",
    [[
        SELECT LEAST(false);
    ]], { 1, "Wrong number of arguments is passed to LEAST(): expected at least two, got 1" } )

test:do_catchsql_test(
    "func-5-5.2",
    [[
        SELECT GREATEST('abc');
    ]], { 1, "Wrong number of arguments is passed to GREATEST(): expected at least two, got 1" } )

test:do_catchsql_test(
    "func-5-5.3",
    [[
        SELECT LEAST();
    ]], { 1, "Wrong number of arguments is passed to LEAST(): expected at least two, got 0" } )

-- Make sure that ifnull() returns type of corresponding (i.e. first
-- non-null) argument.
--
test:do_execsql_test(
    "func-6.1-ifnull",
    [[
        SELECT ifnull('qqq1', 'qqq2') = 'qqq2';
    ]], { false } )

test:do_execsql_test(
    "func-6.2-ifnull",
    [[
        SELECT ifnull(null, 'qqq2') = 'qqq2';
    ]], { true } )

test:do_execsql_test(
    "func-6.3-ifnull",
    [[
        SELECT ifnull(null, 1) = 'qqq2';
    ]], { false } )

box.func.COUNTER1:drop()
box.func.COUNTER2:drop()

test:finish_test()
