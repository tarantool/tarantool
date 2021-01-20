#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(0)

--!./tcltestrunner.lua
-- 2007 May 1
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
-- $Id: icu.test,v 1.2 2008/07/12 14:52:20 drh Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- MUST_WORK_TEST
if (0 > 0)
 then
    -- Create a table to work with.
    --
    test:execsql "CREATE TABLE test1(i1 int primary key, i2 int, r1 INT real, r2 INT real, t1 text, t2 text)"
    test:execsql "INSERT INTO test1 VALUES(1,2,1.1,2.2,'hello','world')"
    local function test_expr(name, settings, expr, result)
        test:do_test(
            name,
            function ()
                return test:execsql(string.format([[
                    START TRANSACTION;
                    UPDATE test1 SET %s;
                    SELECT %s FROM test1;
                    ROLLBACK;
                    ]], settings, expr))
            end,
            {result}
        )
    end

    -- Tests of the REGEXP operator.
    --
    -- MUST_WORK_TEST
--    test_expr("icu-1.1", "i1='hello'", "i1 REGEXP 'hello'", 1)
--    test_expr("icu-1.2", "i1='hello'", "i1 REGEXP '.ello'", 1)
--    test_expr("icu-1.3", "i1='hello'", "i1 REGEXP '.ell'", 0)
--    test_expr("icu-1.4", "i1='hello'", "i1 REGEXP '.ell.*'", 1)
--    test_expr("icu-1.5", "i1=NULL", "i1 REGEXP '.ell.*'", "")
    -- Some non-ascii characters with defined case mappings
    --
    local EGRAVE = "\xC8"
    local egrave = "\xE8"
    local OGRAVE = "\xD2"
    local ograve = "\xF2"
    -- That German letter that looks a bit like a B. The
    -- upper-case version of which is "SS" (two characters).
    --
    local szlig = "\xDF"
    -- Tests of the upper()/lower() functions.
    --
    test_expr("icu-2.1", "i1='HellO WorlD'", "upper(i1)", "HELLO WORLD")
    test_expr("icu-2.2", "i1='HellO WorlD'", "lower(i1)", "hello world")
    test_expr("icu-2.3", "i1='"..egrave.."'", "lower(i1)", egrave)
    test_expr("icu-2.4", "i1='"..egrave.."'", "upper(i1)", EGRAVE)
    test_expr("icu-2.5", "i1='"..ograve.."'", "lower(i1)", ograve)
    test_expr("icu-2.6", "i1='"..ograve.."'", "upper(i1)", OGRAVE)
    test_expr("icu-2.3", "i1='"..EGRAVE.."'", "lower(i1)", egrave)
    test_expr("icu-2.4", "i1='"..EGRAVE.."'", "upper(i1)", EGRAVE)
    test_expr("icu-2.5", "i1='"..OGRAVE.."'", "lower(i1)", ograve)
    test_expr("icu-2.6", "i1='"..OGRAVE.."'", "upper(i1)", OGRAVE)
    test_expr("icu-2.7", "i1='"..szlig.."'", "upper(i1)", "SS")
    test_expr("icu-2.8", "i1='SS'", "lower(i1)", "ss")
    -- In turkish (locale="tr_TR"), the lower case version of I
    -- is "small dotless i" (code point 0x131 (decimal 305)).
    --
    local small_dotless_i = "ı"
    test_expr("icu-3.1", "i1='I'", "lower(i1)", "i")
    test_expr("icu-3.2", "i1='I'", "lower(i1, 'tr_tr')", small_dotless_i)
    test_expr("icu-3.3", "i1='I'", "lower(i1, 'en_AU')", "i")
    ----------------------------------------------------------------------
    -- Test the collation sequence function.
    --
    test:do_execsql_test(
        "icu-4.1",
        [[
            CREATE TABLE fruit(name INT);
            INSERT INTO fruit VALUES('plum');
            INSERT INTO fruit VALUES('cherry');
            INSERT INTO fruit VALUES('apricot');
            INSERT INTO fruit VALUES('peach');
            INSERT INTO fruit VALUES('chokecherry');
            INSERT INTO fruit VALUES('yamot');
        ]], {
            -- <icu-4.1>

            -- </icu-4.1>
        })

    test:do_test(
        "icu-4.2",
        function()
            test:execsql [[
                SELECT icu_load_collation('en_US', 'AmericanEnglish');
                SELECT icu_load_collation('lt_LT', 'Lithuanian');
            ]]
            return test:execsql [[
                SELECT name FROM fruit ORDER BY name COLLATE AmericanEnglish ASC;
            ]]
        end, {
            -- <icu-4.2>
            "apricot", "cherry", "chokecherry", "peach", "plum", "yamot"
            -- </icu-4.2>
        })

    -- Test collation using Lithuanian rules. In the Lithuanian
    -- alphabet, "y" comes right after "i".
    --
    test:do_execsql_test(
        "icu-4.3",
        [[
            SELECT name FROM fruit ORDER BY name COLLATE Lithuanian ASC;
        ]], {
            -- <icu-4.3>
            "apricot", "cherry", "chokecherry", "yamot", "peach", "plum"
            -- </icu-4.3>
        })

    ---------------------------------------------------------------------------
    -- Test that it is not possible to call the ICU regex() function with
    -- anything other than exactly two arguments. See also:
    --
    --   http://src.chromium.org/viewvc/chrome/trunk/src/third_party/sql/icu-regexp.patch?revision=34807&view=markup
    --
    test:do_catchsql_test(
        "icu-5.1",
        " SELECT regexp('a[abc]c.*', 'abc') ", {
            -- <icu-5.1>
            0, 1
            -- </icu-5.1>
        })

    test:do_catchsql_test(
        "icu-5.2",
        [=[
            SELECT regexp('a[abc]c.*')
        ]=], {
            -- <icu-5.2>
            1, "wrong number of arguments to function REGEXP()"
            -- </icu-5.2>
        })

    test:do_catchsql_test(
        "icu-5.3",
        [=[
            SELECT regexp('a[abc]c.*', 'abc', 'c')
        ]=], {
            -- <icu-5.3>
            1, "wrong number of arguments to function REGEXP()"
            -- </icu-5.3>
        })

    test:do_catchsql_test(
        "icu-5.4",
        [=[
            SELECT 'abc' REGEXP 'a[abc]c.*'
        ]=], {
            -- <icu-5.4>
            0, 1
            -- </icu-5.4>
        })

    test:do_catchsql_test(
        "icu-5.4",
        " SELECT 'abc' REGEXP ", {
            -- <icu-5.4>
            1, [[near " ": syntax error]]
            -- </icu-5.4>
        })

    test:do_catchsql_test(
        "icu-5.5",
        " SELECT 'abc' REGEXP, 1 ", {
            -- <icu-5.5>
            1, [[near ",": syntax error]]
            -- </icu-5.5>
        })

end


test:finish_test()
