#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(12431)

--!./tcltestrunner.lua
-- 2010 July 16
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
-- This file implements tests to verify that the "testable statements" in
-- the lang_expr.html document are correct.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- ["source",[["testdir"],"\/malloc_common.tcl"]]


local function do_expr_test(tn, expr, type, value)
    test:do_execsql_test(
        ""..tn,
        "SELECT typeof("..expr.."), "..expr,
        {type, value })
end

local function do_qexpr_test(tn, expr, value)
    test:do_execsql_test(
        ""..tn,
        "SELECT quote("..expr..")",
        {value })
end

local function matchfunc(a, b)
    return (a == b)
end

local function regexfunc(a, b)
    return (a == b)
end
box.internal.sql_create_function("MATCH", matchfunc)
box.internal.sql_create_function("REGEXP", regexfunc)

-- Set up three global variables:
--
--   ::opname         An array mapping from SQL operator to an easy to parse
--                    name. The names are used as part of test case names.
--
--   ::opprec         An array mapping from SQL operator to a numeric
--                    precedence value. Operators that group more tightly
--                    have lower numeric precedences.
--
--   ::oplist         A list of all SQL operators supported by SQLite.
--
local operations = {
    {"||", "cat"},
    {"*", "mul"},
    {"/", "div"},
    {"%", "mod"},
    {"+", "add"},
    {"-", "sub"},
    {"<<", "lshift"},
    {">>", "rshift"},
    {"&", "bitand"},
    {"|", "bitor"},
    {"<", "less"},
    {"<=", "lesseq"},
    {">", "more"},
    {">=", "moreeq"},
    {"=", "eq1"},
    {"==", "eq2"},
    {"<>", "ne1"},
    {"!=", "ne2"},
    {"IS", "is"},
    {"LIKE", "like"},
    {"GLOB", "glob"},
    {"AND", "and"},
    {"OR", "or"},
    {"MATCH", "match"},
    {"REGEXP", "regexp"},
    {"IS NOT", "isnt"}
}
local opname = {}
for _, op in ipairs(operations) do
    opname[op[1]] = op[2]
end

operations = {
    {"||"},
    {"*", "/", "%"},
    {"+", "-"},
    {"<<", ">>", "&", "|"},
    {"<", "<=", ">", ">="},
    {"=", "==", "!=", "<>", "LIKE", "GLOB"}, --"MATCH", "REGEXP"},
    {"AND"},
    {"OR"},
}
local oplist = {  }
local opprec = {  }
for prec, opl in ipairs(operations) do
    for _, op in ipairs(opl) do
        opprec[op] = prec
        table.insert(oplist,op)
    end
end
-- Hook in definitions of MATCH and REGEX. The following implementations
-- cause MATCH and REGEX to behave similarly to the == operator.
--


---------------------------------------------------------------------------
-- Test cases e_expr-1.* attempt to verify that all binary operators listed
-- in the documentation exist and that the relative precedences of the
-- operators are also as the documentation suggests.
--
-- EVIDENCE-OF: R-15514-65163 SQLite understands the following binary
-- operators, in order from highest to lowest precedence: || * / % + -
-- << >> & | < <= > >= = == != <> IS IS
-- NOT IN LIKE GLOB MATCH REGEXP AND OR
--
-- EVIDENCE-OF: R-38759-38789 Operators IS and IS NOT have the same
-- precedence as =.
--
local test_cases1 = {
    {1, 22, 45, 66},
    {2, 0, 0, 0},
    {3, 0, 0, 1},
    {4, 0, 1, 0},
    {5, 0, 1, 1},
    {6, 1, 0, 0},
    {7, 1, 0, 1},
    {8, 1, 1, 0},
    {9, 1, 1, 1},
    {10, 5, 6, 1},
    {11, 1, 5, 6},
    {12, 1, 5, 5},
    {13, 5, 5, 1},

    {14, 5, 2, 1},
    {15, 1, 4, 1},
    {16, -1, 0, 1},
    {17, 0, 1, -1},
}

local untested = {}
for _, op1 in ipairs(oplist) do
    for _, op2 in ipairs(oplist) do
        untested[op1..","..op2] = 1
        for tn, val in ipairs(test_cases1) do
            local A = val[1]
            local B = val[2]
            local C = val[3]
            local testname = string.format("e_expr-1.%s.%s.%s", opname[op1], opname[op2], tn)
            -- If ?op2 groups more tightly than ?op1, then the result
            -- of executing ?sql1 whould be the same as executing ?sql3.
            -- If ?op1 groups more tightly, or if ?op1 and ?op2 have
            -- the same precedence, then executing ?sql1 should return
            -- the same value as ?sql2.
            --
            local sql1 = string.format("SELECT %s %s %s %s %s", A, op1, B, op2, C)
            local sql2 = string.format("SELECT (%s %s %s) %s %s", A, op1, B, op2, C)
            local sql3 = string.format("SELECT %s %s (%s %s %s)", A, op1, B, op2, C)
            local a2 = test:execsql(sql2)
            local a3 = test:execsql(sql3)
            test:do_execsql_test(
                testname,
                sql1, (opprec[op2] < opprec[op1]) and a3 or a2)

            if (a2 ~= a3) then
                untested[op1..","..op2] = nil
            end
        end
    end
end
for _, op in ipairs({"op","*", "AND", "OR", "+", "||", "&", "|"}) do
    untested[op..","..op] = nil
end
---- ["unset","untested(+,-)"]
----       Since    (a+b)-c == a+(b-c)
---- ["unset","untested(*,<<)"]
----       Since    (a*b)<<c == a*(b<<c)
test:do_test(
    "e_expr-1.1",
    function()
        return untested
    end, {
        -- <e_expr-1.1>

        -- </e_expr-1.1>
    })

-- At one point, test 1.2.2 was failing. Instead of the correct result, it
-- was returning {1 1 0}. This would seem to indicate that LIKE has the
-- same precedence as '<'. Which is incorrect. It has lower precedence.
--
test:do_execsql_test(
    "e_expr-1.2.1",
    [[
        SELECT 0 < 2 LIKE 1,   (0 < 2) LIKE 1,   0 < (2 LIKE 1)
    ]], {
        -- <e_expr-1.2.1>
        1, 1, 0
        -- </e_expr-1.2.1>
    })

test:do_execsql_test(
    "e_expr-1.2.2",
    [[
        SELECT 0 LIKE 0 < 2,   (0 LIKE 0) < 2,   0 LIKE (0 < 2)
    ]], {
        -- <e_expr-1.2.2>
        0, 1, 0
        -- </e_expr-1.2.2>
    })

-- Showing that LIKE and == have the same precedence
--
test:do_execsql_test(
    "e_expr-1.2.3",
    [[
        SELECT 2 LIKE 2 == 1,   (2 LIKE 2) == 1,    2 LIKE (2 == 1)
    ]], {
        -- <e_expr-1.2.3>
        1, 1, 0
        -- </e_expr-1.2.3>
    })

test:do_execsql_test(
    "e_expr-1.2.4",
    [[
        SELECT 2 == 2 LIKE 1,   (2 == 2) LIKE 1,    2 == (2 LIKE 1)
    ]], {
        -- <e_expr-1.2.4>
        1, 1, 0
        -- </e_expr-1.2.4>
    })

-- Showing that < groups more tightly than == (< has higher precedence).
--
test:do_execsql_test(
    "e_expr-1.2.5",
    [[
        SELECT 0 < 2 == 1,   (0 < 2) == 1,   0 < (2 == 1)
    ]], {
        -- <e_expr-1.2.5>
        1, 1, 0
        -- </e_expr-1.2.5>
    })

test:do_execsql_test(
    "e_expr-1.6",
    [[
        SELECT 0 == 0 < 2,   (0 == 0) < 2,   0 == (0 < 2)
    ]], {
        -- <e_expr-1.6>
        0, 1, 0
        -- </e_expr-1.6>
    })

---------------------------------------------------------------------------
-- Check that the four unary prefix operators mentioned in the
-- documentation exist.
--
-- EVIDENCE-OF: R-13958-53419 Supported unary prefix operators are these:
-- - + ~ NOT
--
test:do_execsql_test(
    "e_expr-2.1",
    [[
        SELECT -   10
    ]], {
        -- <e_expr-2.1>
        -10
        -- </e_expr-2.1>
    })

test:do_execsql_test(
    "e_expr-2.2",
    [[
        SELECT +   10
    ]], {
        -- <e_expr-2.2>
        10
        -- </e_expr-2.2>
    })

test:do_execsql_test(
    "e_expr-2.3",
    [[
        SELECT ~   10
    ]], {
        -- <e_expr-2.3>
        -11
        -- </e_expr-2.3>
    })

test:do_execsql_test(
    "e_expr-2.4",
    [[
        SELECT NOT 10
    ]], {
        -- <e_expr-2.4>
        0
        -- </e_expr-2.4>
    })

---------------------------------------------------------------------------
-- Tests for the two statements made regarding the unary + operator.
--
-- EVIDENCE-OF: R-53670-03373 The unary operator + is a no-op.
--
-- EVIDENCE-OF: R-19480-30968 It can be applied to strings, numbers,
-- blobs or NULL and it always returns a result with the same value as
-- the operand.
--
local literals = {
    {1, "'helloworld'", "text"},
    {2, 45, "integer"},
    {3, "45.2", "real"},
    {4, "45.0", "real"},
    {5, "X'ABCDEF'", "blob"},
    {6, "NULL", "null"},
}
for _, val in ipairs(literals) do
    local tn = val[1]
    local literal = val[2]
    local type = val[3]
    local sql = string.format(" SELECT quote( + %s ), typeof( + %s) ", literal, literal)
    test:do_execsql_test(
        "e_expr-3."..tn,
        sql, {
            literal, type
        })

end
---------------------------------------------------------------------------
-- Check that both = and == are both acceptable as the "equals" operator.
-- Similarly, either != or <> work as the not-equals operator.
--
-- EVIDENCE-OF: R-03679-60639 Equals can be either = or ==.
--
-- EVIDENCE-OF: R-30082-38996 The non-equals operator can be either != or
-- <>.
--
literals = {
    {1, "'helloworld'", '12345'},
    {2, 22, 23},
    {3, "'xyz'", "X'78797A'"},
    {4, "X'78797A00'", "'xyz'"}
}
for _, val in ipairs(literals) do
    local tn = val[1]
    local literal = val[2]
    local different = val[3]
    test:do_execsql_test(
        "e_expr-4."..tn,
        string.format([[
            SELECT %s  = %s,   %s == %s,
                   %s  = %s, %s == %s,
                   %s  = NULL,       %s == NULL,
                   %s != %s,   %s <> %s,
                   %s != %s, %s <> %s,
                   %s != NULL,       %s != NULL

        ]], literal, literal, literal, literal, literal, different, literal, different, literal, literal, literal, literal, literal, literal, literal, different, literal, different, literal, literal), {
            1, 1, 0, 0, "", "", 0, 0, 1, 1, "", ""
        })

end
---------------------------------------------------------------------------
-- Test the || operator.
--
-- EVIDENCE-OF: R-44409-62641 The || operator is "concatenate" - it joins
-- together the two strings of its operands.
--
local test_cases5 = {
    {1, "'helloworld'",  "'12345'"},
    {2, 22, 23}
}
for _, val in ipairs(test_cases5) do
    local tn = val[1]
    local a = val[2]
    local b = val[3]
    local as = test:execsql("SELECT "..a.."")[1]
    local bs = test:execsql("SELECT "..b.."")[1]
    test:do_execsql_test(
        "e_expr-5."..tn,
        string.format([[
            SELECT %s || %s
        ]], a, b), {
            string.format("%s%s", as, bs)
        })

end
---------------------------------------------------------------------------
-- Test the % operator.
--
-- EVIDENCE-OF: R-08914-63790 The operator % outputs the value of its
-- left operand modulo its right operand.
--
test:do_execsql_test(
    "e_expr-6.1",
    [[
        SELECT  72%5
    ]], {
        -- <e_expr-6.1>
        2
        -- </e_expr-6.1>
    })

test:do_execsql_test(
    "e_expr-6.2",
    [[
        SELECT  72%-5
    ]], {
        -- <e_expr-6.2>
        2
        -- </e_expr-6.2>
    })

test:do_execsql_test(
    "e_expr-6.3",
    [[
        SELECT -72%-5
    ]], {
        -- <e_expr-6.3>
        -2
        -- </e_expr-6.3>
    })

test:do_execsql_test(
    "e_expr-6.4",
    [[
        SELECT -72%5
    ]], {
        -- <e_expr-6.4>
        -2
        -- </e_expr-6.4>
    })

---------------------------------------------------------------------------
-- Test that the results of all binary operators are either numeric or
-- NULL, except for the || operator, which may evaluate to either a text
-- value or NULL.
--
-- EVIDENCE-OF: R-20665-17792 The result of any binary operator is either
-- a numeric value or NULL, except for the || concatenation operator
-- which always evaluates to either NULL or a text value.
--
literals = {
  "'abc'", "'hexadecimal'", "''", 123, -123, 0,
    123.4, 0.0, -123.4, "X'ABCDEF'", "X''",
    "X'0000'", "NULL"}

for _, op in ipairs(oplist) do
    for n1, rhs in ipairs(literals) do
        for n2, lhs in ipairs(literals) do
            local t = test:execsql(string.format(" SELECT typeof(%s %s %s) ", lhs, op, rhs))[1]
            test:do_test(
                string.format("e_expr-7.%s.%s.%s", opname[op], n1, n2),
                function()
                    --print("\n op "..op.." t "..t)
                    return (((op == "||") and ((t == "text") or
                            (t == "null"))) or
                            ((op ~= "||") and (((t == "integer") or
                                    (t == "real")) or
                                    (t == "null")))) and 1 or 0
                end, 1)

        end
    end
end
---------------------------------------------------------------------------
-- Test the IS and IS NOT operators.
--
-- EVIDENCE-OF: R-24731-45773 The IS and IS NOT operators work like = and
-- != except when one or both of the operands are NULL.
--
-- EVIDENCE-OF: R-06325-15315 In this case, if both operands are NULL,
-- then the IS operator evaluates to 1 (true) and the IS NOT operator
-- evaluates to 0 (false).
--
-- EVIDENCE-OF: R-19812-36779 If one operand is NULL and the other is
-- not, then the IS operator evaluates to 0 (false) and the IS NOT
-- operator is 1 (true).
--
-- EVIDENCE-OF: R-61975-13410 It is not possible for an IS or IS NOT
-- expression to evaluate to NULL.
--
test:do_execsql_test(
    "e_expr-8.1.1",
    [[
        SELECT NULL IS     NULL
    ]], {
        -- <e_expr-8.1.1>
        1
        -- </e_expr-8.1.1>
    })

test:do_execsql_test(
    "e_expr-8.1.2",
    [[
        SELECT 'ab' IS     NULL
    ]], {
        -- <e_expr-8.1.2>
        0
        -- </e_expr-8.1.2>
    })

test:do_execsql_test(
    "e_expr-8.1.3",
    [[
        SELECT NULL ==     NULL
    ]], {
        -- <e_expr-8.1.5>
        ""
        -- </e_expr-8.1.5>
    })

test:do_execsql_test(
    "e_expr-8.1.4",
    [[
        SELECT 'ab' ==     NULL
    ]], {
        -- <e_expr-8.1.6>
        ""
        -- </e_expr-8.1.6>
    })

test:do_execsql_test(
    "e_expr-8.1.5",
    [[
        SELECT NULL ==     'ab'
    ]], {
        -- <e_expr-8.1.7>
        ""
        -- </e_expr-8.1.7>
    })

test:do_execsql_test(
    "e_expr-8.1.6",
    [[
        SELECT 'ab' ==     'ab'
    ]], {
        -- <e_expr-8.1.8>
        1
        -- </e_expr-8.1.8>
    })

test:do_execsql_test(
    "e_expr-8.1.7",
    [[
        SELECT NULL IS NOT NULL
    ]], {
        -- <e_expr-8.1.9>
        0
        -- </e_expr-8.1.9>
    })

test:do_execsql_test(
    "e_expr-8.1.8",
    [[
        SELECT 'ab' IS NOT NULL
    ]], {
        -- <e_expr-8.1.10>
        1
        -- </e_expr-8.1.10>
    })

test:do_execsql_test(
    "e_expr-8.1.9",
    [[
        SELECT NULL !=     NULL
    ]], {
        -- <e_expr-8.1.13>
        ""
        -- </e_expr-8.1.13>
    })

test:do_execsql_test(
    "e_expr-8.1.10",
    [[
        SELECT 'ab' !=     NULL
    ]], {
        -- <e_expr-8.1.14>
        ""
        -- </e_expr-8.1.14>
    })

test:do_execsql_test(
    "e_expr-8.1.11",
    [[
        SELECT NULL !=     'ab'
    ]], {
        -- <e_expr-8.1.15>
        ""
        -- </e_expr-8.1.15>
    })

test:do_execsql_test(
    "e_expr-8.1.12",
    [[
        SELECT 'ab' !=     'ab'
    ]], {
        -- <e_expr-8.1.16>
        0
        -- </e_expr-8.1.16>
    })

---------------------------------------------------------------------------
-- Run some tests on the COLLATE "unary postfix operator".
--
-- This collation sequence reverses both arguments before using
-- [string compare] to compare them. For example, when comparing the
-- strings 'one' and 'four', return the result of:
--
--   string compare eno ruof
--

local function reverse_str(zStr)
    local out = ""
    for i = 1, #zStr, 1 do
        out = string.format("%s%s", string.sub(zStr, i, i), out)
    end
    return out
end

local function reverse_collate(zLeft, zRight)
    return reverse_str(zLeft) > reverse_str(zRight)
end
box.internal.sql_create_function("REVERSE", reverse_collate)
--db("collate", "reverse", "reverse_collate")
-- EVIDENCE-OF: R-59577-33471 The COLLATE operator is a unary postfix
-- operator that assigns a collating sequence to an expression.
--
-- EVIDENCE-OF: R-36231-30731 The COLLATE operator has a higher
-- precedence (binds more tightly) than any binary operator and any unary
-- prefix operator except "~".
--
-- MUST_WORK_TEST waiting for collations?
if (0>0) then
    test:do_execsql_test(
        "e_expr-9.1",
        [[
            SELECT  'abcd' < 'bbbb'    COLLATE reverse
        ]], {
            -- <e_expr-9.1>
            0
            -- </e_expr-9.1>
        })

    test:do_execsql_test(
        "e_expr-9.2",
        [[
            SELECT ('abcd' < 'bbbb')   COLLATE reverse
        ]], {
            -- <e_expr-9.2>
            1
            -- </e_expr-9.2>
        })

    test:do_execsql_test(
        "e_expr-9.3",
        [[
            SELECT  'abcd' <= 'bbbb'   COLLATE reverse
        ]], {
            -- <e_expr-9.3>
            0
            -- </e_expr-9.3>
        })

    test:do_execsql_test(
        "e_expr-9.4",
        [[
            SELECT ('abcd' <= 'bbbb')  COLLATE reverse
        ]], {
            -- <e_expr-9.4>
            1
            -- </e_expr-9.4>
        })

    test:do_execsql_test(
        "e_expr-9.5",
        [[
            SELECT  'abcd' > 'bbbb'    COLLATE reverse
        ]], {
            -- <e_expr-9.5>
            1
            -- </e_expr-9.5>
        })

    test:do_execsql_test(
        "e_expr-9.6",
        [[
            SELECT ('abcd' > 'bbbb')   COLLATE reverse
        ]], {
            -- <e_expr-9.6>
            0
            -- </e_expr-9.6>
        })

    test:do_execsql_test(
        "e_expr-9.7",
        [[
            SELECT  'abcd' >= 'bbbb'   COLLATE reverse
        ]], {
            -- <e_expr-9.7>
            1
            -- </e_expr-9.7>
        })

    test:do_execsql_test(
        "e_expr-9.8",
        [[
            SELECT ('abcd' >= 'bbbb')  COLLATE reverse
        ]], {
            -- <e_expr-9.8>
            0
            -- </e_expr-9.8>
        })
end
test:do_execsql_test(
    "e_expr-9.10",
    [[
        SELECT  'abcd' =  'ABCD'  COLLATE "unicode_ci"
    ]], {
        -- <e_expr-9.10>
        1
        -- </e_expr-9.10>
    })

test:do_execsql_test(
    "e_expr-9.11",
    [[
        SELECT ('abcd' =  'ABCD') COLLATE "unicode_ci"
    ]], {
        -- <e_expr-9.11>
        0
        -- </e_expr-9.11>
    })

test:do_execsql_test(
    "e_expr-9.12",
    [[
        SELECT  'abcd' == 'ABCD'  COLLATE "unicode_ci"
    ]], {
        -- <e_expr-9.12>
        1
        -- </e_expr-9.12>
    })

test:do_execsql_test(
    "e_expr-9.13",
    [[
        SELECT ('abcd' == 'ABCD') COLLATE "unicode_ci"
    ]], {
        -- <e_expr-9.13>
        0
        -- </e_expr-9.13>
    })

test:do_execsql_test(
    "e_expr-9.14",
    [[
        SELECT  'abcd' != 'ABCD'      COLLATE "unicode_ci"
    ]], {
        -- <e_expr-9.16>
        0
        -- </e_expr-9.16>
    })

test:do_execsql_test(
    "e_expr-9.15",
    [[
        SELECT ('abcd' != 'ABCD')     COLLATE "unicode_ci"
    ]], {
        -- <e_expr-9.17>
        1
        -- </e_expr-9.17>
    })

test:do_execsql_test(
    "e_expr-9.16",
    [[
        SELECT  'abcd' <> 'ABCD'      COLLATE "unicode_ci"
    ]], {
        -- <e_expr-9.18>
        0
        -- </e_expr-9.18>
    })

test:do_execsql_test(
    "e_expr-9.17",
    [[
        SELECT ('abcd' <> 'ABCD')     COLLATE "unicode_ci"
    ]], {
        -- <e_expr-9.19>
        1
        -- </e_expr-9.19>
    })

test:do_execsql_test(
    "e_expr-9.19",
    [[
        SELECT 'bbb' BETWEEN 'AAA' AND 'CCC' COLLATE "unicode_ci"
    ]], {
        -- <e_expr-9.22>
        1
        -- </e_expr-9.22>
    })

test:do_execsql_test(
    "e_expr-9.20",
    [[
        SELECT ('bbb' BETWEEN 'AAA' AND 'CCC') COLLATE "unicode_ci"
    ]], {
        -- <e_expr-9.23>
        0
        -- </e_expr-9.23>
    })

-- # EVIDENCE-OF: R-58731-25439 The collating sequence set by the COLLATE
-- # operator overrides the collating sequence determined by the COLLATE
-- # clause in a table column definition.
-- #
-- do_execsql_test e_expr-9.24 {
--   CREATE TABLE t24(a COLLATE NOCASE, b);
--   INSERT INTO t24 VALUES('aaa', 1);
--   INSERT INTO t24 VALUES('bbb', 2);
--   INSERT INTO t24 VALUES('ccc', 3);
-- } {}
-- do_execsql_test e_expr-9.25 { SELECT 'BBB' = a FROM t24 } {0 1 0}
-- do_execsql_test e_expr-9.25 { SELECT a = 'BBB' FROM t24 } {0 1 0}
-- do_execsql_test e_expr-9.25 { SELECT 'BBB' = a COLLATE binary FROM t24 } {0 0 0}
-- do_execsql_test e_expr-9.25 { SELECT a COLLATE binary = 'BBB' FROM t24 } {0 0 0}
---------------------------------------------------------------------------
-- Test statements related to literal values.
--
-- EVIDENCE-OF: R-31536-32008 Literal values may be integers, floating
-- point numbers, strings, BLOBs, or NULLs.
--
test:do_execsql_test(
    "e_expr-10.1.1",
    [[
        SELECT typeof(5)
    ]], {
        -- <e_expr-10.1.1>
        "integer"
        -- </e_expr-10.1.1>
    })

test:do_execsql_test(
    "e_expr-10.1.2",
    [[
        SELECT typeof(5.1)
    ]], {
        -- <e_expr-10.1.2>
        "real"
        -- </e_expr-10.1.2>
    })

test:do_execsql_test(
    "e_expr-10.1.3",
    [[
        SELECT typeof('5.1')
    ]], {
        -- <e_expr-10.1.3>
        "text"
        -- </e_expr-10.1.3>
    })

test:do_execsql_test(
    "e_expr-10.1.4",
    [[
        SELECT typeof(X'ABCD')
    ]], {
        -- <e_expr-10.1.4>
        "blob"
        -- </e_expr-10.1.4>
    })

test:do_execsql_test(
    "e_expr-10.1.5",
    [[
        SELECT typeof(NULL)
    ]], {
        -- <e_expr-10.1.5>
        "null"
        -- </e_expr-10.1.5>
    })

-- "Scientific notation is supported for point literal values."
--
test:do_execsql_test(
    "e_expr-10.2.1",
    [[
        SELECT typeof(3.4e-02)
    ]], {
        -- <e_expr-10.2.1>
        "real"
        -- </e_expr-10.2.1>
    })

test:do_execsql_test(
    "e_expr-10.2.2",
    [[
        SELECT typeof(3e+5)
    ]], {
        -- <e_expr-10.2.2>
        "real"
        -- </e_expr-10.2.2>
    })

test:do_execsql_test(
    "e_expr-10.2.3",
    [[
        SELECT 3.4e-02
    ]], {
        -- <e_expr-10.2.3>
        0.034
        -- </e_expr-10.2.3>
    })

test:do_execsql_test(
    "e_expr-10.2.4",
    [[
        SELECT 3e+4
    ]], {
        -- <e_expr-10.2.4>
        30000.0
        -- </e_expr-10.2.4>
    })

-- EVIDENCE-OF: R-35229-17830 A string constant is formed by enclosing
-- the string in single quotes (').
--
-- EVIDENCE-OF: R-07100-06606 A single quote within the string can be
-- encoded by putting two single quotes in a row - as in Pascal.
--
test:do_execsql_test(
    "e_expr-10.3.1",
    [[
        SELECT 'is not'
    ]], {
        -- <e_expr-10.3.1>
        "is not"
        -- </e_expr-10.3.1>
    })

test:do_execsql_test(
    "e_expr-10.3.2",
    [[
        SELECT typeof('is not')
    ]], {
        -- <e_expr-10.3.2>
        "text"
        -- </e_expr-10.3.2>
    })

test:do_execsql_test(
    "e_expr-10.3.3",
    [[
        SELECT 'isn''t'
    ]], {
        -- <e_expr-10.3.3>
        "isn't"
        -- </e_expr-10.3.3>
    })

test:do_execsql_test(
    "e_expr-10.3.4",
    [[
        SELECT typeof('isn''t')
    ]], {
        -- <e_expr-10.3.4>
        "text"
        -- </e_expr-10.3.4>
    })

-- EVIDENCE-OF: R-09593-03321 BLOB literals are string literals
-- containing hexadecimal data and preceded by a single "x" or "X"
-- character.
--
-- EVIDENCE-OF: R-19836-11244 Example: X'53514C697465'
--
test:do_execsql_test(
    "e_expr-10.4.1",
    [[
        SELECT typeof(X'0123456789ABCDEF')
    ]], {
        -- <e_expr-10.4.1>
        "blob"
        -- </e_expr-10.4.1>
    })

test:do_execsql_test(
    "e_expr-10.4.2",
    [[
        SELECT typeof(x'0123456789ABCDEF')
    ]], {
        -- <e_expr-10.4.2>
        "blob"
        -- </e_expr-10.4.2>
    })

test:do_execsql_test(
    "e_expr-10.4.3",
    [[
        SELECT typeof(X'0123456789abcdef')
    ]], {
        -- <e_expr-10.4.3>
        "blob"
        -- </e_expr-10.4.3>
    })

test:do_execsql_test(
    "e_expr-10.4.4",
    [[
        SELECT typeof(x'0123456789abcdef')
    ]], {
        -- <e_expr-10.4.4>
        "blob"
        -- </e_expr-10.4.4>
    })

test:do_execsql_test(
    "e_expr-10.4.5",
    [[
        SELECT typeof(X'53514C697465')
    ]], {
        -- <e_expr-10.4.5>
        "blob"
        -- </e_expr-10.4.5>
    })

-- EVIDENCE-OF: R-23914-51476 A literal value can also be the token
-- "NULL".
--
test:do_execsql_test(
    "e_expr-10.5.1",
    [[
        SELECT NULL
    ]], {
        -- <e_expr-10.5.1>
        ""
        -- </e_expr-10.5.1>
    })

test:do_execsql_test(
    "e_expr-10.5.2",
    [[
        SELECT typeof(NULL)
    ]], {
        -- <e_expr-10.5.2>
        "null"
        -- </e_expr-10.5.2>
    })

---------------------------------------------------------------------------
-- Test statements related to bound parameters
--
-- MUST_WORK_TEST prepared statements
if (0>0) then
    local function parameter_test(tn, sql, params, result)
        stmt = sqlite3_prepare_v2("db", sql, -1)
        for _ in X(0, "X!foreach", [=[["number name",["params"]]]=]) do
            nm = sqlite3_bind_parameter_name(stmt, number)
            X(480, "X!cmd", [=[["do_test",[["tn"],".name.",["number"]],[["list","set","",["nm"]]],["name"]]]=])
            sqlite3_bind_int(stmt, number, ((-1) * number))
        end
        sqlite3_step(stmt)
        res = {  }
        for _ in X(0, "X!for", [=[["set i 0","$i < [sqlite3_column_count $stmt]","incr i"]]=]) do
            table.insert(res,sqlite3_column_text(stmt, i))
        end
        rc = sqlite3_finalize(stmt)
        X(491, "X!cmd", [=[["do_test",[["tn"],".rc"],[["list","set","",["rc"]]],"SQLITE_OK"]]=])
        X(492, "X!cmd", [=[["do_test",[["tn"],".res"],[["list","set","",["res"]]],["result"]]]=])
    end

    -- EVIDENCE-OF: R-11620-22743 A colon followed by an identifier name
    -- holds a spot for a named parameter with the name :AAAA.
    --
    -- Identifiers in SQLite consist of alphanumeric, '_' and '$' characters,
    -- and any UTF characters with codepoints larger than 127 (non-ASCII
    -- characters).
    --
    parameter_test("e_expr-11.2.1", "SELECT :AAAA", "1 :AAAA", -1)
    parameter_test("e_expr-11.2.2", "SELECT :123", "1 :123", -1)
    parameter_test("e_expr-11.2.3", "SELECT :__", "1 :__", -1)
    parameter_test("e_expr-11.2.4", "SELECT :_$_", "1 :_$_", -1)
    parameter_test("e_expr-11.2.5", [[
      SELECT :เอศขูเอล
    ]], "1 :เอศขูเอล", -1)
    parameter_test("e_expr-11.2.6", "SELECT :", "1 :", -1)
    -- EVIDENCE-OF: R-49783-61279 An "at" sign works exactly like a colon,
    -- except that the name of the parameter created is @AAAA.
    --
    parameter_test("e_expr-11.3.1", "SELECT @AAAA", "1 @AAAA", -1)
    parameter_test("e_expr-11.3.2", "SELECT @123", "1 @123", -1)
    parameter_test("e_expr-11.3.3", "SELECT @__", "1 @__", -1)
    parameter_test("e_expr-11.3.4", "SELECT @_$_", "1 @_$_", -1)
    parameter_test("e_expr-11.3.5", [[
      SELECT @เอศขูเอล
    ]], "1 @เอศขูเอล", -1)
    parameter_test("e_expr-11.3.6", "SELECT @", "1 @", -1)
    -- EVIDENCE-OF: R-14068-49671 Parameters that are not assigned values
    -- using sqlite3_bind() are treated as NULL.
    --
    test:do_test(
        "e_expr-11.7.1",
        function()
            stmt = sqlite3_prepare_v2("db", " SELECT ?, :a, @b, ?d ", -1)
            sqlite3_step(stmt)
            return { sqlite3_column_type(stmt, 0), sqlite3_column_type(stmt, 1), sqlite3_column_type(stmt, 2), sqlite3_column_type(stmt, 3) }
        end, {
            -- <e_expr-11.7.1>
            "NULL", "NULL", "NULL", "NULL"
            -- </e_expr-11.7.1>
        })

    test:do_sqlite3_finalize_test(
        "e_expr-11.7.1",
        stmt, {
            -- <e_expr-11.7.1>
            "SQLITE_OK"
            -- </e_expr-11.7.1>
        })
end
---------------------------------------------------------------------------
-- "Test" the syntax diagrams in lang_expr.html.
--
-- -- syntax diagram signed-number
--
test:do_execsql_test(
    "e_expr-12.1.1",
    [[
        SELECT 0, +0, -0
    ]], {
        -- <e_expr-12.1.1>
        0, 0, 0
        -- </e_expr-12.1.1>
    })

test:do_execsql_test(
    "e_expr-12.1.2",
    [[
        SELECT 1, +1, -1
    ]], {
        -- <e_expr-12.1.2>
        1, 1, -1
        -- </e_expr-12.1.2>
    })

test:do_execsql_test(
    "e_expr-12.1.3",
    [[
        SELECT 2, +2, -2
    ]], {
        -- <e_expr-12.1.3>
        2, 2, -2
        -- </e_expr-12.1.3>
    })

test:do_execsql_test(
    "e_expr-12.1.4",
    [[
        SELECT 1.4, +1.4, -1.4
    ]], {
        -- <e_expr-12.1.4>
        1.4, 1.4, -1.4
        -- </e_expr-12.1.4>
    })

test:do_execsql_test(
    "e_expr-12.1.5",
    [[
        SELECT 1.5e+5, +1.5e+5, -1.5e+5
    ]], {
        -- <e_expr-12.1.5>
        150000.0, 150000.0, -150000.0
        -- </e_expr-12.1.5>
    })

test:do_execsql_test(
    "e_expr-12.1.6",
    [[
        SELECT 0.0001, +0.0001, -0.0001
    ]], {
        -- <e_expr-12.1.6>
        0.0001, 0.0001, -0.0001
        -- </e_expr-12.1.6>
    })

-- -- syntax diagram literal-value
--

test:do_execsql_test(
    "e_expr-12.2.1",
    [[
        SELECT 123
    ]], {
        -- <e_expr-12.2.1>
        123
        -- </e_expr-12.2.1>
    })

test:do_execsql_test(
    "e_expr-12.2.2",
    [[
        SELECT 123.4e05
    ]], {
        -- <e_expr-12.2.2>
        12340000.0
        -- </e_expr-12.2.2>
    })

test:do_execsql_test(
    "e_expr-12.2.3",
    [[
        SELECT 'abcde'
    ]], {
        -- <e_expr-12.2.3>
        "abcde"
        -- </e_expr-12.2.3>
    })

test:do_execsql_test(
    "e_expr-12.2.4",
    [[
        SELECT X'414243'
    ]], {
        -- <e_expr-12.2.4>
        "ABC"
        -- </e_expr-12.2.4>
    })

test:do_execsql_test(
    "e_expr-12.2.5",
    [[
        SELECT NULL
    ]], {
        -- <e_expr-12.2.5>
        ""
        -- </e_expr-12.2.5>
    })

-- MUST_WORK_TEST uses sqlite_current_time
if 0>0 then
    sqlite_current_time = 1
    test:do_execsql_test(
        "e_expr-12.2.6",
        [[
            SELECT CURRENT_TIME
        ]], {
            -- <e_expr-12.2.6>
            "00:00:01"
            -- </e_expr-12.2.6>
        })

    test:do_execsql_test(
        "e_expr-12.2.7",
        [[
            SELECT CURRENT_DATE
        ]], {
            -- <e_expr-12.2.7>
            "1970-01-01"
            -- </e_expr-12.2.7>
        })

    test:do_execsql_test(
        "e_expr-12.2.8",
        [[
            SELECT CURRENT_TIMESTAMP
        ]], {
            -- <e_expr-12.2.8>
            "1970-01-01 00:00:01"
            -- </e_expr-12.2.8>
        })

    sqlite_current_time = 0
end
-- # -- syntax diagram expr
-- #
-- forcedelete test.db2
-- execsql {
--   ATTACH 'test.db2' AS dbname;
--   CREATE TABLE dbname.tblname(cname);
-- }
test:execsql [[
    CREATE TABLE tblname(cname PRIMARY KEY);
]]
local function glob(args)
    return 1
end

box.internal.sql_create_function("GLOB", glob)
box.internal.sql_create_function("MATCH", glob)
box.internal.sql_create_function("REGEXP", glob)
local test_cases12 ={
    {1, 123},
    {2, 123.4e05},
    {3, "'abcde'"},
    {4, "X'414243'"},
    {5, "NULL"},
    {6, "CURRENT_TIME"},
    {7, "CURRENT_DATE"},
    {8, "CURRENT_TIMESTAMP"},

    {9, "?"},
    {10, "@hello"},
    {11, ":world"},

    {12, "cname"},
    {13, "tblname.cname"},

    {14, "+ EXPR"},
    {15, "- EXPR"},
    {16, "NOT EXPR"},
    {17, "~ EXPR"},

    {18, "EXPR1 || EXPR2"},
    {19, "EXPR1 * EXPR2"},
    {20, "EXPR1 / EXPR2"},
    {21, "EXPR1 % EXPR2"},
    {22, "EXPR1 + EXPR2"},
    {23, "EXPR1 - EXPR2"},
    {24, "EXPR1 << EXPR2"},
    {25, "EXPR1 >> EXPR2"},
    {26, "EXPR1 & EXPR2"},

    {27, "EXPR1 | EXPR2"},
    {28, "EXPR1 < EXPR2"},
    {29, "EXPR1 <= EXPR2"},
    {30, "EXPR1 > EXPR2"},
    {31, "EXPR1 >= EXPR2"},
    {32, "EXPR1 = EXPR2"},
    {33, "EXPR1 == EXPR2"},
    {34, "EXPR1 != EXPR2"},
    {35, "EXPR1 <> EXPR2"},
    {36, "EXPR1 AND EXPR2"},
    {37, "EXPR1 OR EXPR2"},

    {38, "count(*)"},
    {39, "count(DISTINCT EXPR)"},
    {40, "substr(EXPR, 10, 20)"},
    {41, "changes()"},

    {42, "( EXPR )"},

    {43, "CAST ( EXPR AS integer )"},
    {44, "CAST ( EXPR AS 'abcd' )"},

    {45, "EXPR COLLATE \"unicode_ci\""},
    {46, "EXPR COLLATE binary"},

    {47, "EXPR1 LIKE EXPR2"},
    {48, "EXPR1 LIKE EXPR2 ESCAPE EXPR"},
    {49, "EXPR1 GLOB EXPR2"},
    {50, "EXPR1 GLOB EXPR2 ESCAPE EXPR"},
    {51, "EXPR1 REGEXP EXPR2"},
    {52, "EXPR1 REGEXP EXPR2 ESCAPE EXPR"},
    {53, "EXPR1 MATCH EXPR2"},
    {54, "EXPR1 MATCH EXPR2 ESCAPE EXPR"},
    {55, "EXPR1 NOT LIKE EXPR2"},
    {56, "EXPR1 NOT LIKE EXPR2 ESCAPE EXPR"},
    {57, "EXPR1 NOT GLOB EXPR2"},
    {58, "EXPR1 NOT GLOB EXPR2 ESCAPE EXPR"},
    {59, "EXPR1 NOT REGEXP EXPR2"},
    {60, "EXPR1 NOT REGEXP EXPR2 ESCAPE EXPR"},
    {61, "EXPR1 NOT MATCH EXPR2"},
    {62, "EXPR1 NOT MATCH EXPR2 ESCAPE EXPR"},

    {63, "EXPR IS NULL"},
    {64, "EXPR IS NOT NULL"},

    {65, "EXPR NOT BETWEEN EXPR1 AND EXPR2"},
    {66, "EXPR BETWEEN EXPR1 AND EXPR2"},

    {67, "EXPR NOT IN (SELECT cname FROM tblname)"},
    {68, "EXPR NOT IN (1)"},
    {69, "EXPR NOT IN (1, 2, 3)"},
    {70, "EXPR NOT IN tblname"},
    {71, "EXPR IN (SELECT cname FROM tblname)"},
    {72, "EXPR IN (1)"},
    {73, "EXPR IN (1, 2, 3)"},
    {74, "EXPR IN tblname"},

    {75, "EXISTS (SELECT cname FROM tblname)"},
    {76, "NOT EXISTS (SELECT cname FROM tblname)"},

    {77, "CASE EXPR WHEN EXPR1 THEN EXPR2 ELSE EXPR END"},
    {78, "CASE EXPR WHEN EXPR1 THEN EXPR2 END"},
    {79, "CASE EXPR WHEN EXPR1 THEN EXPR2 WHEN EXPR THEN EXPR1 ELSE EXPR2 END"},
    {80, "CASE EXPR WHEN EXPR1 THEN EXPR2 WHEN EXPR THEN EXPR1 END"},
    {81, "CASE WHEN EXPR1 THEN EXPR2 ELSE EXPR END"},
    {82, "CASE WHEN EXPR1 THEN EXPR2 END"},
    {83, "CASE WHEN EXPR1 THEN EXPR2 WHEN EXPR THEN EXPR1 ELSE EXPR2 END"},
    {84, "CASE WHEN EXPR1 THEN EXPR2 WHEN EXPR THEN EXPR1 END"},
}

for _, val in ipairs(test_cases12) do
    -- If the expression string being parsed contains "EXPR2", then replace
    -- string "EXPR1" and "EXPR2" with arbitrary SQL expressions. If it
    -- contains "EXPR", then replace EXPR with an arbitrary SQL expression.
    --
    local tn = val[1]
    local expr = val[2]
    local elist = { expr }
    if string.find(expr, "EXPR2") then
        elist = {}
        local e1 = "cname"
        local e2 = "34+22"
        local result = expr
        result = string.gsub(result, "EXPR1", e1)
        result = string.gsub(result, "EXPR2", e2)
        table.insert(elist, result)
    end
    if string.find(expr, "EXPR") then
        local elist2 = {  }
        for _, el in ipairs(elist) do
            for _, e in ipairs({"cname", "34+22"}) do
                local result = string.gsub(el, "EXPR", e)
                table.insert(elist2, result)
            end
        end
        elist = elist2
    end
    local x = 0
    for _, e in ipairs(elist) do
        x = x + 1
        test:do_test(
            string.format("e_expr-12.3.%s.%s", tn, x),
            function()
                local rc, err = pcall( function()
                    test:execsql("SELECT "..e.." FROM tblname")
                end)
                return rc
                --return set("rc", X(712, "X!cmd", [=[["catch"," execsql \"SELECT $e FROM tblname\" ","msg"]]=]))
            end, true)

    end
end
---------------------------------------------------------------------------
-- Test the statements related to the BETWEEN operator.
--
-- EVIDENCE-OF: R-40079-54503 The BETWEEN operator is logically
-- equivalent to a pair of comparisons. "x BETWEEN y AND z" is equivalent
-- to "x>=y AND x<=z" except that with BETWEEN, the x expression is
-- only evaluated once.
--
local xcount = 0
local x = 0
local function func_x()
    xcount = xcount + 1
    return x
end
box.internal.sql_create_function("X", func_x)
local test_cases13 = {
    {1, 10, "x() >= 5 AND x() <= 15", 1, 2},
    {2, 10, "x() BETWEEN 5 AND 15", 1, 1},

    {3, 5, "x() >= 5 AND x() <= 5", 1, 2},
    {4, 5, "x() BETWEEN 5 AND 5", 1, 1}
}

for _, val in ipairs(test_cases13) do
    local tn = val[1]
    x = val[2]
    local expr = val[3]
    local res = val[4]
    local nEval = val[5]
    test:do_test(
        "e_expr-13.1."..tn,
        function()
            xcount = 0
            local a = test:execsql("SELECT "..expr.."")[1]
            return { xcount, a }
        end, {
            nEval, res
        })

end
-- EVIDENCE-OF: R-05155-34454 The precedence of the BETWEEN operator is
-- the same as the precedence as operators == and != and LIKE and groups
-- left to right.
--
-- Therefore, BETWEEN groups more tightly than operator "AND", but less
-- so than "<".
--
test:do_execsql_test(
    "e_expr-13.2.1",
    [[
        SELECT 1 == 10 BETWEEN 0 AND 2
    ]], {
        -- <e_expr-13.2.1>
        1
        -- </e_expr-13.2.1>
    })

test:do_execsql_test(
    "e_expr-13.2.2",
    [[
        SELECT (1 == 10) BETWEEN 0 AND 2
    ]], {
        -- <e_expr-13.2.2>
        1
        -- </e_expr-13.2.2>
    })

test:do_execsql_test(
    "e_expr-13.2.3",
    [[
        SELECT 1 == (10 BETWEEN 0 AND 2)
    ]], {
        -- <e_expr-13.2.3>
        0
        -- </e_expr-13.2.3>
    })

test:do_execsql_test(
    "e_expr-13.2.4",
    [[
        SELECT  6 BETWEEN 4 AND 8 == 1
    ]], {
        -- <e_expr-13.2.4>
        1
        -- </e_expr-13.2.4>
    })

test:do_execsql_test(
    "e_expr-13.2.5",
    [[
        SELECT (6 BETWEEN 4 AND 8) == 1
    ]], {
        -- <e_expr-13.2.5>
        1
        -- </e_expr-13.2.5>
    })

test:do_execsql_test(
    "e_expr-13.2.6",
    [[
        SELECT  6 BETWEEN 4 AND (8 == 1)
    ]], {
        -- <e_expr-13.2.6>
        0
        -- </e_expr-13.2.6>
    })

test:do_execsql_test(
    "e_expr-13.2.7",
    [[
        SELECT  5 BETWEEN 0 AND 0  != 1
    ]], {
        -- <e_expr-13.2.7>
        1
        -- </e_expr-13.2.7>
    })

test:do_execsql_test(
    "e_expr-13.2.8",
    [[
        SELECT (5 BETWEEN 0 AND 0) != 1
    ]], {
        -- <e_expr-13.2.8>
        1
        -- </e_expr-13.2.8>
    })

test:do_execsql_test(
    "e_expr-13.2.9",
    [[
        SELECT  5 BETWEEN 0 AND (0 != 1)
    ]], {
        -- <e_expr-13.2.9>
        0
        -- </e_expr-13.2.9>
    })

test:do_execsql_test(
    "e_expr-13.2.10",
    [[
        SELECT  1 != 0  BETWEEN 0 AND 2
    ]], {
        -- <e_expr-13.2.10>
        1
        -- </e_expr-13.2.10>
    })

test:do_execsql_test(
    "e_expr-13.2.11",
    [[
        SELECT (1 != 0) BETWEEN 0 AND 2
    ]], {
        -- <e_expr-13.2.11>
        1
        -- </e_expr-13.2.11>
    })

test:do_execsql_test(
    "e_expr-13.2.12",
    [[
        SELECT  1 != (0 BETWEEN 0 AND 2)
    ]], {
        -- <e_expr-13.2.12>
        0
        -- </e_expr-13.2.12>
    })

test:do_execsql_test(
    "e_expr-13.2.13",
    [[
        SELECT 1 LIKE 10 BETWEEN 0 AND 2
    ]], {
        -- <e_expr-13.2.13>
        1
        -- </e_expr-13.2.13>
    })

test:do_execsql_test(
    "e_expr-13.2.14",
    [[
        SELECT (1 LIKE 10) BETWEEN 0 AND 2
    ]], {
        -- <e_expr-13.2.14>
        1
        -- </e_expr-13.2.14>
    })

test:do_execsql_test(
    "e_expr-13.2.15",
    [[
        SELECT 1 LIKE (10 BETWEEN 0 AND 2)
    ]], {
        -- <e_expr-13.2.15>
        0
        -- </e_expr-13.2.15>
    })

test:do_execsql_test(
    "e_expr-13.2.16",
    [[
        SELECT  6 BETWEEN 4 AND 8 LIKE 1
    ]], {
        -- <e_expr-13.2.16>
        1
        -- </e_expr-13.2.16>
    })

test:do_execsql_test(
    "e_expr-13.2.17",
    [[
        SELECT (6 BETWEEN 4 AND 8) LIKE 1
    ]], {
        -- <e_expr-13.2.17>
        1
        -- </e_expr-13.2.17>
    })

test:do_execsql_test(
    "e_expr-13.2.18",
    [[
        SELECT  6 BETWEEN 4 AND (8 LIKE 1)
    ]], {
        -- <e_expr-13.2.18>
        0
        -- </e_expr-13.2.18>
    })

test:do_execsql_test(
    "e_expr-13.2.19",
    [[
        SELECT 0 AND 0 BETWEEN 0 AND 1
    ]], {
        -- <e_expr-13.2.19>
        0
        -- </e_expr-13.2.19>
    })

test:do_execsql_test(
    "e_expr-13.2.20",
    [[
        SELECT 0 AND (0 BETWEEN 0 AND 1)
    ]], {
        -- <e_expr-13.2.20>
        0
        -- </e_expr-13.2.20>
    })

test:do_execsql_test(
    "e_expr-13.2.21",
    [[
        SELECT (0 AND 0) BETWEEN 0 AND 1
    ]], {
        -- <e_expr-13.2.21>
        1
        -- </e_expr-13.2.21>
    })

test:do_execsql_test(
    "e_expr-13.2.22",
    [[
        SELECT 0 BETWEEN -1 AND 1 AND 0
    ]], {
        -- <e_expr-13.2.22>
        0
        -- </e_expr-13.2.22>
    })

test:do_execsql_test(
    "e_expr-13.2.23",
    [[
        SELECT (0 BETWEEN -1 AND 1) AND 0
    ]], {
        -- <e_expr-13.2.23>
        0
        -- </e_expr-13.2.23>
    })

test:do_execsql_test(
    "e_expr-13.2.24",
    [[
        SELECT 0 BETWEEN -1 AND (1 AND 0)
    ]], {
        -- <e_expr-13.2.24>
        1
        -- </e_expr-13.2.24>
    })

test:do_execsql_test(
    "e_expr-13.2.25",
    [[
        SELECT 2 < 3 BETWEEN 0 AND 1
    ]], {
        -- <e_expr-13.2.25>
        1
        -- </e_expr-13.2.25>
    })

test:do_execsql_test(
    "e_expr-13.2.26",
    [[
        SELECT (2 < 3) BETWEEN 0 AND 1
    ]], {
        -- <e_expr-13.2.26>
        1
        -- </e_expr-13.2.26>
    })

test:do_execsql_test(
    "e_expr-13.2.27",
    [[
        SELECT 2 < (3 BETWEEN 0 AND 1)
    ]], {
        -- <e_expr-13.2.27>
        0
        -- </e_expr-13.2.27>
    })

test:do_execsql_test(
    "e_expr-13.2.28",
    [[
        SELECT 2 BETWEEN 1 AND 2 < 3
    ]], {
        -- <e_expr-13.2.28>
        0
        -- </e_expr-13.2.28>
    })

test:do_execsql_test(
    "e_expr-13.2.29",
    [[
        SELECT 2 BETWEEN 1 AND (2 < 3)
    ]], {
        -- <e_expr-13.2.29>
        0
        -- </e_expr-13.2.29>
    })

test:do_execsql_test(
    "e_expr-13.2.30",
    [[
        SELECT (2 BETWEEN 1 AND 2) < 3
    ]], {
        -- <e_expr-13.2.30>
        1
        -- </e_expr-13.2.30>
    })

---------------------------------------------------------------------------
-- Test the statements related to the LIKE and GLOB operators.
--
-- EVIDENCE-OF: R-16584-60189 The LIKE operator does a pattern matching
-- comparison.
--
-- EVIDENCE-OF: R-11295-04657 The operand to the right of the LIKE
-- operator contains the pattern and the left hand operand contains the
-- string to match against the pattern.
--
test:do_execsql_test(
    "e_expr-14.1.1",
    [[
        SELECT 'abc%' LIKE 'abcde'
    ]], {
        -- <e_expr-14.1.1>
        0
        -- </e_expr-14.1.1>
    })

test:do_execsql_test(
    "e_expr-14.1.2",
    [[
        SELECT 'abcde' LIKE 'abc%'
    ]], {
        -- <e_expr-14.1.2>
        1
        -- </e_expr-14.1.2>
    })

-- EVIDENCE-OF: R-55406-38524 A percent symbol ("%") in the LIKE pattern
-- matches any sequence of zero or more characters in the string.
--
test:do_execsql_test(
    "e_expr-14.2.1",
    [[
        SELECT 'abde'    LIKE 'ab%de'
    ]], {
        -- <e_expr-14.2.1>
        1
        -- </e_expr-14.2.1>
    })

test:do_execsql_test(
    "e_expr-14.2.2",
    [[
        SELECT 'abXde'   LIKE 'ab%de'
    ]], {
        -- <e_expr-14.2.2>
        1
        -- </e_expr-14.2.2>
    })

test:do_execsql_test(
    "e_expr-14.2.3",
    [[
        SELECT 'abABCde' LIKE 'ab%de'
    ]], {
        -- <e_expr-14.2.3>
        1
        -- </e_expr-14.2.3>
    })

-- EVIDENCE-OF: R-30433-25443 An underscore ("_") in the LIKE pattern
-- matches any single character in the string.
--
test:do_execsql_test(
    "e_expr-14.3.1",
    [[
        SELECT 'abde'    LIKE 'ab_de'
    ]], {
        -- <e_expr-14.3.1>
        0
        -- </e_expr-14.3.1>
    })

test:do_execsql_test(
    "e_expr-14.3.2",
    [[
        SELECT 'abXde'   LIKE 'ab_de'
    ]], {
        -- <e_expr-14.3.2>
        1
        -- </e_expr-14.3.2>
    })

test:do_execsql_test(
    "e_expr-14.3.3",
    [[
        SELECT 'abABCde' LIKE 'ab_de'
    ]], {
        -- <e_expr-14.3.3>
        0
        -- </e_expr-14.3.3>
    })

-- EVIDENCE-OF: R-59007-20454 Any other character matches itself or its
-- lower/upper case equivalent (i.e. case-insensitive matching).
--
test:do_execsql_test(
    "e_expr-14.4.1",
    [[
        SELECT 'abc' LIKE 'aBc'
    ]], {
        -- <e_expr-14.4.1>
        1
        -- </e_expr-14.4.1>
    })

test:do_execsql_test(
    "e_expr-14.4.2",
    [[
        SELECT 'aBc' LIKE 'aBc'
    ]], {
        -- <e_expr-14.4.2>
        1
        -- </e_expr-14.4.2>
    })

test:do_execsql_test(
    "e_expr-14.4.3",
    [[
        SELECT 'ac'  LIKE 'aBc'
    ]], {
        -- <e_expr-14.4.3>
        0
        -- </e_expr-14.4.3>
    })

-- EVIDENCE-OF: R-23648-58527 SQLite only understands upper/lower case
-- for ASCII characters by default.
--
-- EVIDENCE-OF: R-04532-11527 The LIKE operator is case sensitive by
-- default for unicode characters that are beyond the ASCII range.
--
-- EVIDENCE-OF: R-44381-11669 the expression
-- 'a'&nbsp;LIKE&nbsp;'A' is TRUE but
-- '&aelig;'&nbsp;LIKE&nbsp;'&AElig;' is FALSE.
--
--   The restriction to ASCII characters does not apply if the ICU
--   library is compiled in. When ICU is enabled SQLite does not act
--   as it does "by default".
--
test:do_execsql_test(
    "e_expr-14.5.1",
    [[
        SELECT 'A' LIKE 'a'
    ]], {
        -- <e_expr-14.5.1>
        1
        -- </e_expr-14.5.1>
    })



-- EVIDENCE-OF: R-56683-13731 If the optional ESCAPE clause is present,
-- then the expression following the ESCAPE keyword must evaluate to a
-- string consisting of a single character.
--
test:do_catchsql_test(
    "e_expr-14.6.1",
    [[
        SELECT 'A' LIKE 'a' ESCAPE '12'
    ]], {
        -- <e_expr-14.6.1>
        1, "ESCAPE expression must be a single character"
        -- </e_expr-14.6.1>
    })

test:do_catchsql_test(
    "e_expr-14.6.2",
    [[
        SELECT 'A' LIKE 'a' ESCAPE ''
    ]], {
        -- <e_expr-14.6.2>
        1, "ESCAPE expression must be a single character"
        -- </e_expr-14.6.2>
    })

test:do_catchsql_test(
    "e_expr-14.6.3",
    "SELECT 'A' LIKE 'a' ESCAPE 'x' ", {
        -- <e_expr-14.6.3>
        0, {1}
        -- </e_expr-14.6.3>
    })

test:do_catchsql_test(
    "e_expr-14.6.4",
    "SELECT 'A' LIKE 'a' ESCAPE 'æ'", {
        -- <e_expr-14.6.4>
        0, {1}
        -- </e_expr-14.6.4>
    })

-- EVIDENCE-OF: R-02045-23762 This character may be used in the LIKE
-- pattern to include literal percent or underscore characters.
--
-- EVIDENCE-OF: R-13345-31830 The escape character followed by a percent
-- symbol (%), underscore (_), or a second instance of the escape
-- character itself matches a literal percent symbol, underscore, or a
-- single escape character, respectively.
--
test:do_execsql_test(
    "e_expr-14.7.1",
    [[
        SELECT 'abc%'  LIKE 'abcX%' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.1>
        1
        -- </e_expr-14.7.1>
    })

test:do_execsql_test(
    "e_expr-14.7.2",
    [[
        SELECT 'abc5'  LIKE 'abcX%' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.2>
        0
        -- </e_expr-14.7.2>
    })

test:do_execsql_test(
    "e_expr-14.7.3",
    [[
        SELECT 'abc'   LIKE 'abcX%' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.3>
        0
        -- </e_expr-14.7.3>
    })

test:do_execsql_test(
    "e_expr-14.7.4",
    [[
        SELECT 'abcX%' LIKE 'abcX%' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.4>
        0
        -- </e_expr-14.7.4>
    })

test:do_execsql_test(
    "e_expr-14.7.5",
    [[
        SELECT 'abc%%' LIKE 'abcX%' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.5>
        0
        -- </e_expr-14.7.5>
    })

test:do_execsql_test(
    "e_expr-14.7.6",
    [[
        SELECT 'abc_'  LIKE 'abcX_' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.6>
        1
        -- </e_expr-14.7.6>
    })

test:do_execsql_test(
    "e_expr-14.7.7",
    [[
        SELECT 'abc5'  LIKE 'abcX_' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.7>
        0
        -- </e_expr-14.7.7>
    })

test:do_execsql_test(
    "e_expr-14.7.8",
    [[
        SELECT 'abc'   LIKE 'abcX_' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.8>
        0
        -- </e_expr-14.7.8>
    })

test:do_execsql_test(
    "e_expr-14.7.9",
    [[
        SELECT 'abcX_' LIKE 'abcX_' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.9>
        0
        -- </e_expr-14.7.9>
    })

test:do_execsql_test(
    "e_expr-14.7.10",
    [[
        SELECT 'abc__' LIKE 'abcX_' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.10>
        0
        -- </e_expr-14.7.10>
    })

test:do_execsql_test(
    "e_expr-14.7.11",
    [[
        SELECT 'abcX'  LIKE 'abcXX' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.11>
        1
        -- </e_expr-14.7.11>
    })

test:do_execsql_test(
    "e_expr-14.7.12",
    [[
        SELECT 'abc5'  LIKE 'abcXX' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.12>
        0
        -- </e_expr-14.7.12>
    })

test:do_execsql_test(
    "e_expr-14.7.13",
    [[
        SELECT 'abc'   LIKE 'abcXX' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.13>
        0
        -- </e_expr-14.7.13>
    })

test:do_execsql_test(
    "e_expr-14.7.14",
    [[
        SELECT 'abcXX' LIKE 'abcXX' ESCAPE 'X'
    ]], {
        -- <e_expr-14.7.14>
        0
        -- </e_expr-14.7.14>
    })

-- EVIDENCE-OF: R-51359-17496 The infix LIKE operator is implemented by
-- calling the application-defined SQL functions like(Y,X) or like(Y,X,Z).
--
local likeargs = {}
function likefunc(...)
    local args = {...}
    for i, v in ipairs(args) do
        table.insert(likeargs, v)
    end
    return 1
end

box.internal.sql_create_function("LIKE", likefunc)
--db("func", "like", "-argcount", 2, "likefunc")
--db("func", "like", "-argcount", 3, "likefunc")
test:do_execsql_test(
    "e_expr-15.1.1",
    [[
        SELECT 'abc' LIKE 'def'
    ]], {
        -- <e_expr-15.1.1>
        1
        -- </e_expr-15.1.1>
    })

test:do_test(
    "e_expr-15.1.2",
    function()
        return likeargs
    end, {
        -- <e_expr-15.1.2>
        "def", "abc"
        -- </e_expr-15.1.2>
    })

likeargs = {  }
test:do_execsql_test(
    "e_expr-15.1.3",
    [[
        SELECT 'abc' LIKE 'def' ESCAPE 'X'
    ]], {
        -- <e_expr-15.1.3>
        1
        -- </e_expr-15.1.3>
    })

test:do_test(
    "e_expr-15.1.4",
    function()
        return likeargs
    end, {
        -- <e_expr-15.1.4>
        "def", "abc", "X"
        -- </e_expr-15.1.4>
    })
--db("close")
--sqlite3("db", "test.db")
-- EVIDENCE-OF: R-22868-25880 The LIKE operator can be made case
-- sensitive using the case_sensitive_like pragma.
--
test:do_execsql_test(
    "e_expr-16.1.1",
    [[
        SELECT 'abcxyz' LIKE 'ABC%'
    ]], {
        -- <e_expr-16.1.1>
        1
        -- </e_expr-16.1.1>
    })

test:do_execsql_test(
    "e_expr-16.1.2",
    [[
        PRAGMA case_sensitive_like = 1
    ]], {
        -- <e_expr-16.1.2>

        -- </e_expr-16.1.2>
    })

test:do_execsql_test(
    "e_expr-16.1.3",
    [[
        SELECT 'abcxyz' LIKE 'ABC%'
    ]], {
        -- <e_expr-16.1.3>
        0
        -- </e_expr-16.1.3>
    })

test:do_execsql_test(
    "e_expr-16.1.4",
    [[
        SELECT 'ABCxyz' LIKE 'ABC%'
    ]], {
        -- <e_expr-16.1.4>
        1
        -- </e_expr-16.1.4>
    })

test:do_execsql_test(
    "e_expr-16.1.5",
    [[
        PRAGMA case_sensitive_like = 0
    ]], {
        -- <e_expr-16.1.5>

        -- </e_expr-16.1.5>
    })

test:do_execsql_test(
    "e_expr-16.1.6",
    [[
        SELECT 'abcxyz' LIKE 'ABC%'
    ]], {
        -- <e_expr-16.1.6>
        1
        -- </e_expr-16.1.6>
    })

test:do_execsql_test(
    "e_expr-16.1.7",
    [[
        SELECT 'ABCxyz' LIKE 'ABC%'
    ]], {
        -- <e_expr-16.1.7>
        1
        -- </e_expr-16.1.7>
    })

-- EVIDENCE-OF: R-52087-12043 The GLOB operator is similar to LIKE but
-- uses the Unix file globbing syntax for its wildcards.
--
-- EVIDENCE-OF: R-09813-17279 Also, GLOB is case sensitive, unlike LIKE.
--
test:do_execsql_test(
    "e_expr-17.1.1",
    [[
        SELECT 'abcxyz' GLOB 'abc%'
    ]], {
        -- <e_expr-17.1.1>
        0
        -- </e_expr-17.1.1>
    })

test:do_execsql_test(
    "e_expr-17.1.2",
    [[
        SELECT 'abcxyz' GLOB 'abc*'
    ]], {
        -- <e_expr-17.1.2>
        1
        -- </e_expr-17.1.2>
    })

test:do_execsql_test(
    "e_expr-17.1.3",
    [[
        SELECT 'abcxyz' GLOB 'abc___'
    ]], {
        -- <e_expr-17.1.3>
        0
        -- </e_expr-17.1.3>
    })

test:do_execsql_test(
    "e_expr-17.1.4",
    [[
        SELECT 'abcxyz' GLOB 'abc???'
    ]], {
        -- <e_expr-17.1.4>
        1
        -- </e_expr-17.1.4>
    })

test:do_execsql_test(
    "e_expr-17.1.5",
    [[
        SELECT 'abcxyz' GLOB 'abc*'
    ]], {
        -- <e_expr-17.1.5>
        1
        -- </e_expr-17.1.5>
    })

test:do_execsql_test(
    "e_expr-17.1.6",
    [[
        SELECT 'ABCxyz' GLOB 'abc*'
    ]], {
        -- <e_expr-17.1.6>
        0
        -- </e_expr-17.1.6>
    })

test:do_execsql_test(
    "e_expr-17.1.7",
    [[
        SELECT 'abcxyz' GLOB 'ABC*'
    ]], {
        -- <e_expr-17.1.7>
        0
        -- </e_expr-17.1.7>
    })

-- EVIDENCE-OF: R-39616-20555 Both GLOB and LIKE may be preceded by the
-- NOT keyword to invert the sense of the test.
--
test:do_execsql_test(
    "e_expr-17.2.1",
    [[
        SELECT 'abcxyz' NOT GLOB 'ABC*'
    ]], {
        -- <e_expr-17.2.1>
        1
        -- </e_expr-17.2.1>
    })

test:do_execsql_test(
    "e_expr-17.2.2",
    [[
        SELECT 'abcxyz' NOT GLOB 'abc*'
    ]], {
        -- <e_expr-17.2.2>
        0
        -- </e_expr-17.2.2>
    })

test:do_execsql_test(
    "e_expr-17.2.3",
    [[
        SELECT 'abcxyz' NOT LIKE 'ABC%'
    ]], {
        -- <e_expr-17.2.3>
        0
        -- </e_expr-17.2.3>
    })

test:do_execsql_test(
    "e_expr-17.2.4",
    [[
        SELECT 'abcxyz' NOT LIKE 'abc%'
    ]], {
        -- <e_expr-17.2.4>
        0
        -- </e_expr-17.2.4>
    })

test:do_execsql_test(
    "e_expr-17.2.5",
    [[
        SELECT 'abdxyz' NOT LIKE 'abc%'
    ]], {
        -- <e_expr-17.2.5>
        1
        -- </e_expr-17.2.5>
    })

-- MUST_WORK_TEST uses access to nullvalue... (sql parameters) and built in functions
if 0>0 then
    db("nullvalue", "null")
    test:do_execsql_test(
        "e_expr-17.2.6",
        [[
            SELECT 'abcxyz' NOT GLOB NULL
        ]], {
            -- <e_expr-17.2.6>
            "null"
            -- </e_expr-17.2.6>
        })

    test:do_execsql_test(
        "e_expr-17.2.7",
        [[
            SELECT 'abcxyz' NOT LIKE NULL
        ]], {
            -- <e_expr-17.2.7>
            "null"
            -- </e_expr-17.2.7>
        })

    test:do_execsql_test(
        "e_expr-17.2.8",
        [[
            SELECT NULL NOT GLOB 'abc*'
        ]], {
            -- <e_expr-17.2.8>
            "null"
            -- </e_expr-17.2.8>
        })

    test:do_execsql_test(
        "e_expr-17.2.9",
        [[
            SELECT NULL NOT LIKE 'ABC%'
        ]], {
            -- <e_expr-17.2.9>
            "null"
            -- </e_expr-17.2.9>
        })

    db("nullvalue", "")
end

-- EVIDENCE-OF: R-39414-35489 The infix GLOB operator is implemented by
-- calling the function glob(Y,X) and can be modified by overriding that
-- function.

local globargs = {}
local function globfunc(...)
    local args = {...}
    for i, v in ipairs(args) do
        table.insert(globargs, v)
    end
    return 1
end
box.internal.sql_create_function("GLOB", globfunc, 2)
--db("func", "glob", "-argcount", 2, "globfunc")

test:do_execsql_test(
    "e_expr-17.3.1",
    [[
        SELECT 'abc' GLOB 'def'
    ]], {
        -- <e_expr-17.3.1>
        1
        -- </e_expr-17.3.1>
    })

test:do_test(
    "e_expr-17.3.2",
    function()
        return globargs
    end, {
        -- <e_expr-17.3.2>
        "def", "abc"
        -- </e_expr-17.3.2>
    })

globargs = {  }
test:do_execsql_test(
    "e_expr-17.3.3",
    [[
        SELECT 'X' NOT GLOB 'Y'
    ]], {
        -- <e_expr-17.3.3>
        0
        -- </e_expr-17.3.3>
    })

test:do_test(
    "e_expr-17.3.4",
    function()
        return globargs
    end, {
        -- <e_expr-17.3.4>
        "Y", "X"
        -- </e_expr-17.3.4>
    })

--sqlite3("db", "test.db")
-- EVIDENCE-OF: R-41650-20872 No regexp() user function is defined by
-- default and so use of the REGEXP operator will normally result in an
-- error message.
--
--   There is a regexp function if ICU is enabled though.
--


-- EVIDENCE-OF: R-33693-50180 The REGEXP operator is a special syntax for
-- the regexp() user function.
--
-- EVIDENCE-OF: R-65524-61849 If an application-defined SQL function
-- named "regexp" is added at run-time, then the "X REGEXP Y" operator
-- will be implemented as a call to "regexp(Y,X)".
--
local regexpargs = {}
local function regexpfunc(...)
    local args = {...}
    for i, v in ipairs(args) do
        table.insert(regexpargs, v)
    end
    return 1
end
box.internal.sql_create_function("REGEXP", regexpfunc, 2)
--db("func", "regexp", "-argcount", 2, "regexpfunc")

test:do_execsql_test(
    "e_expr-18.2.1",
    [[
        SELECT 'abc' REGEXP 'def'
    ]], {
        -- <e_expr-18.2.1>
        1
        -- </e_expr-18.2.1>
    })

test:do_test(
    "e_expr-18.2.2",
    function()
        return regexpargs
    end, {
        -- <e_expr-18.2.2>
        "def", "abc"
        -- </e_expr-18.2.2>
    })

regexpargs = {  }
test:do_execsql_test(
    "e_expr-18.2.3",
    [[
        SELECT 'X' NOT REGEXP 'Y'
    ]], {
        -- <e_expr-18.2.3>
        0
        -- </e_expr-18.2.3>
    })

test:do_test(
    "e_expr-18.2.4",
    function()
        return regexpargs
    end, {
        -- <e_expr-18.2.4>
        "Y", "X"
        -- </e_expr-18.2.4>
    })

--sqlite3("db", "test.db")
-- EVIDENCE-OF: R-42037-37826 The default match() function implementation
-- raises an exception and is not really useful for anything.
--
test:do_catchsql_test(
    "e_expr-19.1.1",
    [[
        SELECT 'abc' MATCH 'def'
    ]], {
        -- <e_expr-19.1.1>
        1, "unable to use function MATCH in the requested context"
        -- </e_expr-19.1.1>
    })

test:do_catchsql_test(
    "e_expr-19.1.2",
    [[
        SELECT match('abc', 'def')
    ]], {
        -- <e_expr-19.1.2>
        1, "unable to use function MATCH in the requested context"
        -- </e_expr-19.1.2>
    })

-- EVIDENCE-OF: R-37916-47407 The MATCH operator is a special syntax for
-- the match() application-defined function.
--
-- EVIDENCE-OF: R-06021-09373 But extensions can override the match()
-- function with more helpful logic.
--

local matchargs = {  }
local function matchfunc(...)
    local args = {...}
    for i, v in ipairs(args) do
        table.insert(matchargs, v)
    end
    return 1
end
box.internal.sql_create_function("MATCH", matchfunc, 2)

test:do_execsql_test(
    "e_expr-19.2.1",
    [[
        SELECT 'abc' MATCH 'def'
    ]], {
        -- <e_expr-19.2.1>
        1
        -- </e_expr-19.2.1>
    })

test:do_test(
    "e_expr-19.2.2",
    function()
        return matchargs
    end, {
        -- <e_expr-19.2.2>
        "def", "abc"
        -- </e_expr-19.2.2>
    })

matchargs = {  }
test:do_execsql_test(
    "e_expr-19.2.3",
    [[
        SELECT 'X' NOT MATCH 'Y'
    ]], {
        -- <e_expr-19.2.3>
        0
        -- </e_expr-19.2.3>
    })

test:do_test(
    "e_expr-19.2.4",
    function()
        return matchargs
    end, {
        -- <e_expr-19.2.4>
        "Y", "X"
        -- </e_expr-19.2.4>
    })
--sqlite3("db", "test.db")
---------------------------------------------------------------------------
-- Test cases for the testable statements related to the CASE expression.
--
-- EVIDENCE-OF: R-15199-61389 There are two basic forms of the CASE
-- expression: those with a base expression and those without.
--
test:do_execsql_test(
    "e_expr-20.1",
    [[
        SELECT CASE WHEN 1 THEN 'true' WHEN 0 THEN 'false' ELSE 'else' END;
    ]], {
        -- <e_expr-20.1>
        "true"
        -- </e_expr-20.1>
    })

test:do_execsql_test(
    "e_expr-20.2",
    [[
        SELECT CASE 0 WHEN 1 THEN 'true' WHEN 0 THEN 'false' ELSE 'else' END;
    ]], {
        -- <e_expr-20.2>
        "false"
        -- </e_expr-20.2>
    })

a = 0
b = 0
c = 0
local varlist = {  }
local function var(nm)
    table.insert(varlist,nm)
    local result = loadstring("return "..nm)()
    return result
end
box.internal.sql_create_function("VAR", var)
--db("func", "var", "var")
-- EVIDENCE-OF: R-30638-59954 In a CASE without a base expression, each
-- WHEN expression is evaluated and the result treated as a boolean,
-- starting with the leftmost and continuing to the right.
--

test:do_execsql_test(
    "e_expr-21.1.1",
    [[
        SELECT CASE WHEN var('a') THEN 'A'
                    WHEN var('b') THEN 'B'
                    WHEN var('c') THEN 'C' END
    ]], {
        -- <e_expr-21.1.1>
        ""
        -- </e_expr-21.1.1>
    })

test:do_test(
    "e_expr-21.1.2",
    function()
        return varlist
    end, {
        -- <e_expr-21.1.2>
        "a", "b", "c"
        -- </e_expr-21.1.2>
    })

varlist = {  }
test:do_execsql_test(
    "e_expr-21.1.3",
    [[
        SELECT CASE WHEN var('c') THEN 'C'
                    WHEN var('b') THEN 'B'
                    WHEN var('a') THEN 'A'
                    ELSE 'no result'
        END
    ]], {
        -- <e_expr-21.1.3>
        "no result"
        -- </e_expr-21.1.3>
    })

test:do_test(
    "e_expr-21.1.4",
    function()
        return varlist
    end, {
        -- <e_expr-21.1.4>
        "c", "b", "a"
        -- </e_expr-21.1.4>
    })

-- EVIDENCE-OF: R-39009-25596 The result of the CASE expression is the
-- evaluation of the THEN expression that corresponds to the first WHEN
-- expression that evaluates to true.
--
a = 0
b = 1
c = 0
test:do_execsql_test(
    "e_expr-21.2.1",
    [[
        SELECT CASE WHEN var('a') THEN 'A'
                    WHEN var('b') THEN 'B'
                    WHEN var('c') THEN 'C'
                    ELSE 'no result'
        END
    ]], {
        -- <e_expr-21.2.1>
        "B"
        -- </e_expr-21.2.1>
    })
a = 0
b = 1
c = 1
test:do_execsql_test(
    "e_expr-21.2.2",
    [[
        SELECT CASE WHEN var('a') THEN 'A'
                    WHEN var('b') THEN 'B'
                    WHEN var('c') THEN 'C'
                    ELSE 'no result'
        END
    ]], {
        -- <e_expr-21.2.2>
        "B"
        -- </e_expr-21.2.2>
    })
a = 0
b = 0
c = 1
test:do_execsql_test(
    "e_expr-21.2.3",
    [[
        SELECT CASE WHEN var('a') THEN 'A'
                    WHEN var('b') THEN 'B'
                    WHEN var('c') THEN 'C'
                    ELSE 'no result'
        END
    ]], {
        -- <e_expr-21.2.3>
        "C"
        -- </e_expr-21.2.3>
    })

-- EVIDENCE-OF: R-24227-04807 Or, if none of the WHEN expressions
-- evaluate to true, the result of evaluating the ELSE expression, if
-- any.
--
a = 0
b = 0
c = 0
test:do_execsql_test(
    "e_expr-21.3.1",
    [[
        SELECT CASE WHEN var('a') THEN 'A'
                    WHEN var('b') THEN 'B'
                    WHEN var('c') THEN 'C'
                    ELSE 'no result'
        END
    ]], {
        -- <e_expr-21.3.1>
        "no result"
        -- </e_expr-21.3.1>
    })

-- EVIDENCE-OF: R-14168-07579 If there is no ELSE expression and none of
-- the WHEN expressions are true, then the overall result is NULL.
--
-- MUST_WORK_TEST uses access to nullvalue
if 0>0 then
    db("nullvalue", "null")
    test:do_execsql_test(
        "e_expr-21.3.2",
        [[
            SELECT CASE WHEN var('a') THEN 'A'
                        WHEN var('b') THEN 'B'
                        WHEN var('c') THEN 'C'
            END
        ]], {
            -- <e_expr-21.3.2>
            "null"
            -- </e_expr-21.3.2>
        })

    db("nullvalue", "")
    -- EVIDENCE-OF: R-13943-13592 A NULL result is considered untrue when
    -- evaluating WHEN terms.
    --
    test:do_execsql_test(
        "e_expr-21.4.1",
        [[
            SELECT CASE WHEN NULL THEN 'A' WHEN 1 THEN 'B' END
        ]], {
            -- <e_expr-21.4.1>
            "B"
            -- </e_expr-21.4.1>
        })

    test:do_execsql_test(
        "e_expr-21.4.2",
        [[
            SELECT CASE WHEN 0 THEN 'A' WHEN NULL THEN 'B' ELSE 'C' END
        ]], {
            -- <e_expr-21.4.2>
            "C"
            -- </e_expr-21.4.2>
        })

    -- EVIDENCE-OF: R-38620-19499 In a CASE with a base expression, the base
    -- expression is evaluated just once and the result is compared against
    -- the evaluation of each WHEN expression from left to right.
    --
    -- Note: This test case tests the "evaluated just once" part of the above
    -- statement. Tests associated with the next two statements test that the
    -- comparisons take place.
    --

    for _ in X(0, "X!foreach", [=[["a b c",[["list",[["expr","3"]],[["expr","4"]],[["expr","5"]]]]]]=]) do
        break
    end
    varlist = {  }
    test:do_execsql_test(
        "e_expr-22.1.1",
        [[
            SELECT CASE var('a') WHEN 1 THEN 'A' WHEN 2 THEN 'B' WHEN 3 THEN 'C' END
        ]], {
            -- <e_expr-22.1.1>
            "C"
            -- </e_expr-22.1.1>
        })

    test:do_test(
        "e_expr-22.1.2",
        function()
            return varlist
        end, {
            -- <e_expr-22.1.2>
            "a"
            -- </e_expr-22.1.2>
        })
end

-- EVIDENCE-OF: R-07667-49537 The result of the CASE expression is the
-- evaluation of the THEN expression that corresponds to the first WHEN
-- expression for which the comparison is true.
--
test:do_execsql_test(
    "e_expr-22.2.1",
    [[
        SELECT CASE 23 WHEN 1 THEN 'A' WHEN 23 THEN 'B' WHEN 23 THEN 'C' END
    ]], {
        -- <e_expr-22.2.1>
        "B"
        -- </e_expr-22.2.1>
    })

test:do_execsql_test(
    "e_expr-22.2.2",
    [[
        SELECT CASE 1 WHEN 1 THEN 'A' WHEN 23 THEN 'B' WHEN 23 THEN 'C' END
    ]], {
        -- <e_expr-22.2.2>
        "A"
        -- </e_expr-22.2.2>
    })

-- EVIDENCE-OF: R-47543-32145 Or, if none of the WHEN expressions
-- evaluate to a value equal to the base expression, the result of
-- evaluating the ELSE expression, if any.
--
test:do_execsql_test(
    "e_expr-22.3.1",
    [[
        SELECT CASE 24 WHEN 1 THEN 'A' WHEN 23 THEN 'B' WHEN 23 THEN 'C' ELSE 'D' END
    ]], {
        -- <e_expr-22.3.1>
        "D"
        -- </e_expr-22.3.1>
    })

-- EVIDENCE-OF: R-54721-48557 If there is no ELSE expression and none of
-- the WHEN expressions produce a result equal to the base expression,
-- the overall result is NULL.
--
test:do_execsql_test(
    "e_expr-22.4.1",
    [[
        SELECT CASE 24 WHEN 1 THEN 'A' WHEN 23 THEN 'B' WHEN 23 THEN 'C' END
    ]], {
        -- <e_expr-22.4.1>
        ""
        -- </e_expr-22.4.1>
    })
-- MUST_WORK_TEST uses access to nullvalue
if 0>0 then
    db("nullvalue", "null")
    test:do_execsql_test(
        "e_expr-22.4.2",
        [[
            SELECT CASE 24 WHEN 1 THEN 'A' WHEN 23 THEN 'B' WHEN 23 THEN 'C' END
        ]], {
            -- <e_expr-22.4.2>
            "null"
            -- </e_expr-22.4.2>
        })

    db("nullvalue", "")
end
-- # EVIDENCE-OF: R-11479-62774 When comparing a base expression against a
-- # WHEN expression, the same collating sequence, affinity, and
-- # NULL-handling rules apply as if the base expression and WHEN
-- # expression are respectively the left- and right-hand operands of an =
-- # operator.
-- #
-- proc rev {str} {
--   set ret ""
--   set chars [split $str]
--   for {set i [expr [llength $chars]-1]} {$i>=0} {incr i -1} {
--     append ret [lindex $chars $i]
--   }
--   set ret
-- }
-- proc reverse {lhs rhs} {
--   string compare [rev $lhs] [rev $rhs]
-- }
-- db collate reverse reverse
-- do_execsql_test e_expr-23.1.1 {
--   CREATE TABLE t1(
--     a TEXT     COLLATE NOCASE,
--     b          COLLATE REVERSE,
--     c INTEGER,
--     d BLOB
--   );
--   INSERT INTO t1 VALUES('abc', 'cba', 55, 34.5);
-- } {}
-- do_execsql_test e_expr-23.1.2 {
--   SELECT CASE a WHEN 'xyz' THEN 'A' WHEN 'AbC' THEN 'B' END FROM t1
-- } {B}
-- do_execsql_test e_expr-23.1.3 {
--   SELECT CASE 'AbC' WHEN 'abc' THEN 'A' WHEN a THEN 'B' END FROM t1
-- } {B}
-- do_execsql_test e_expr-23.1.4 {
--   SELECT CASE a WHEN b THEN 'A' ELSE 'B' END FROM t1
-- } {B}
-- do_execsql_test e_expr-23.1.5 {
--   SELECT CASE b WHEN a THEN 'A' ELSE 'B' END FROM t1
-- } {B}
-- do_execsql_test e_expr-23.1.6 {
--   SELECT CASE 55 WHEN '55' THEN 'A' ELSE 'B' END
-- } {B}
-- do_execsql_test e_expr-23.1.7 {
--   SELECT CASE c WHEN '55' THEN 'A' ELSE 'B' END FROM t1
-- } {A}
-- do_execsql_test e_expr-23.1.8 {
--   SELECT CASE '34.5' WHEN d THEN 'A' ELSE 'B' END FROM t1
-- } {B}
-- do_execsql_test e_expr-23.1.9 {
--   SELECT CASE NULL WHEN NULL THEN 'A' ELSE 'B' END
-- } {B}
-- EVIDENCE-OF: R-37304-39405 If the base expression is NULL then the
-- result of the CASE is always the result of evaluating the ELSE
-- expression if it exists, or NULL if it does not.
--
test:do_execsql_test(
    "e_expr-24.1.1",
    [[
        SELECT CASE NULL WHEN 'abc' THEN 'A' WHEN 'def' THEN 'B' END;
    ]], {
        -- <e_expr-24.1.1>
        ""
        -- </e_expr-24.1.1>
    })

test:do_execsql_test(
    "e_expr-24.1.2",
    [[
        SELECT CASE NULL WHEN 'abc' THEN 'A' WHEN 'def' THEN 'B' ELSE 'C' END;
    ]], {
        -- <e_expr-24.1.2>
        "C"
        -- </e_expr-24.1.2>
    })

-- EVIDENCE-OF: R-56280-17369 Both forms of the CASE expression use lazy,
-- or short-circuit, evaluation.
--
varlist = {}
a = "0"
b = "1"
c = "0"
test:do_execsql_test(
    "e_expr-25.1.1",
    [[
        SELECT CASE WHEN var('a') THEN 'A'
                    WHEN var('b') THEN 'B'
                    WHEN var('c') THEN 'C'
        END
    ]], {
        -- <e_expr-25.1.1>
        "B"
        -- </e_expr-25.1.1>
    })

test:do_test(
    "e_expr-25.1.2",
    function()
        return varlist
    end, {
        -- <e_expr-25.1.2>
        "a", "b"
        -- </e_expr-25.1.2>
    })

varlist = {  }
test:do_execsql_test(
    "e_expr-25.1.3",
    [[
        SELECT CASE '0' WHEN var('a') THEN 'A'
                        WHEN var('b') THEN 'B'
                        WHEN var('c') THEN 'C'
        END
    ]], {
        -- <e_expr-25.1.3>
        "A"
        -- </e_expr-25.1.3>
    })

test:do_test(
    "e_expr-25.1.4",
    function()
        return varlist
    end, {
        -- <e_expr-25.1.4>
        "a"
        -- </e_expr-25.1.4>
    })

-- EVIDENCE-OF: R-34773-62253 The only difference between the following
-- two CASE expressions is that the x expression is evaluated exactly
-- once in the first example but might be evaluated multiple times in the
-- second: CASE x WHEN w1 THEN r1 WHEN w2 THEN r2 ELSE r3 END CASE WHEN
-- x=w1 THEN r1 WHEN x=w2 THEN r2 ELSE r3 END
--
local evalcount = 0
local function ceval(x)
    evalcount = evalcount + 1
    return x
end
box.internal.sql_create_function("CEVAL", ceval)
evalcount = 0
test:do_execsql_test(
    "e_expr-26.1.1",
    [[
        CREATE TABLE t2(x PRIMARY KEY, w1, r1, w2, r2, r3);
        INSERT INTO t2 VALUES(1, 1, 'R1', 2, 'R2', 'R3');
        INSERT INTO t2 VALUES(2, 1, 'R1', 2, 'R2', 'R3');
        INSERT INTO t2 VALUES(3, 1, 'R1', 2, 'R2', 'R3');
    ]], {
        -- <e_expr-26.1.1>

        -- </e_expr-26.1.1>
    })

test:do_execsql_test(
    "e_expr-26.1.2",
    [[
        SELECT CASE x WHEN w1 THEN r1 WHEN w2 THEN r2 ELSE r3 END FROM t2
    ]], {
        -- <e_expr-26.1.2>
        "R1", "R2", "R3"
        -- </e_expr-26.1.2>
    })

test:do_execsql_test(
    "e_expr-26.1.3",
    [[
        SELECT CASE WHEN x=w1 THEN r1 WHEN x=w2 THEN r2 ELSE r3 END FROM t2
    ]], {
        -- <e_expr-26.1.3>
        "R1", "R2", "R3"
        -- </e_expr-26.1.3>
    })

test:do_execsql_test(
    "e_expr-26.1.4",
    [[
        SELECT CASE ceval(x) WHEN w1 THEN r1 WHEN w2 THEN r2 ELSE r3 END FROM t2
    ]], {
        -- <e_expr-26.1.4>
        "R1", "R2", "R3"
        -- </e_expr-26.1.4>
    })

test:do_test(
    "e_expr-26.1.5",
    function()
        return evalcount
    end, 3)

evalcount = 0
test:do_execsql_test(
    "e_expr-26.1.6",
    [[
        SELECT CASE
          WHEN ceval(x)=w1 THEN r1
          WHEN ceval(x)=w2 THEN r2
          ELSE r3 END
        FROM t2
    ]], {
        -- <e_expr-26.1.6>
        "R1", "R2", "R3"
        -- </e_expr-26.1.6>
    })

test:do_test(
    "e_expr-26.1.6",
    function()
        return evalcount
    end, 5)


---------------------------------------------------------------------------
-- Test statements related to CAST expressions.
--
-- EVIDENCE-OF: R-20854-17109 A CAST conversion is similar to the
-- conversion that takes place when a column affinity is applied to a
-- value except that with the CAST operator the conversion always takes
-- place even if the conversion lossy and irreversible, whereas column
-- affinity only changes the data type of a value if the change is
-- lossless and reversible.
--
test:do_execsql_test(
    "e_expr-27.1.1",
    [[
        CREATE TABLE t3(a TEXT PRIMARY KEY, b REAL, c INTEGER);
        INSERT INTO t3 VALUES(X'555655', '1.23abc', 4.5);
        SELECT typeof(a), a, typeof(b), b, typeof(c), c FROM t3;
    ]], {
        -- <e_expr-27.1.1>
        "blob", "UVU", "text", "1.23abc", "real", 4.5
        -- </e_expr-27.1.1>
    })

test:do_execsql_test(
    "e_expr-27.1.2",
    [[
        SELECT
          typeof(CAST(X'555655' as TEXT)), CAST(X'555655' as TEXT),
          typeof(CAST('1.23abc' as REAL)), CAST('1.23abc' as REAL),
          typeof(CAST(4.5 as INTEGER)), CAST(4.5 as INTEGER)
    ]], {
        -- <e_expr-27.1.2>
        "text", "UVU", "real", 1.23, "integer", 4
        -- </e_expr-27.1.2>
    })

-- EVIDENCE-OF: R-32434-09092 If the value of expr is NULL, then the
-- result of the CAST expression is also NULL.
--
do_expr_test("e_expr-27.2.1", " CAST(NULL AS integer) ", "null", "")
do_expr_test("e_expr-27.2.2", " CAST(NULL AS text) ", "null", "")
do_expr_test("e_expr-27.2.3", " CAST(NULL AS blob) ", "null", "")
do_expr_test("e_expr-27.2.4", " CAST(NULL AS number) ", "null", "")
-- EVIDENCE-OF: R-43522-35548 Casting a value to a type-name with no
-- affinity causes the value to be converted into a BLOB.
--
do_expr_test("e_expr-27.3.1", " CAST('abc' AS blob)       ", "blob", "abc")
do_expr_test("e_expr-27.3.2", " CAST('def' AS shobblob_x) ", "blob", "def")
do_expr_test("e_expr-27.3.3", " CAST('ghi' AS abbLOb10)   ", "blob", "ghi")
-- EVIDENCE-OF: R-22956-37754 Casting to a BLOB consists of first casting
-- the value to TEXT in the encoding of the database connection, then
-- interpreting the resulting byte sequence as a BLOB instead of as TEXT.
--
do_qexpr_test("e_expr-27.4.1", " CAST('ghi' AS blob) ", "X'676869'")
do_qexpr_test("e_expr-27.4.2", " CAST(456 AS blob) ", "X'343536'")
do_qexpr_test("e_expr-27.4.3", " CAST(1.78 AS blob) ", "X'312E3738'")

-- EVIDENCE-OF: R-22235-47006 Casting an INTEGER or REAL value into TEXT
-- renders the value as if via sqlite3_snprintf() except that the
-- resulting TEXT uses the encoding of the database connection.
--
do_expr_test("e_expr-28.2.1", " CAST (1 AS text)   ", "text", "1")
do_expr_test("e_expr-28.2.2", " CAST (45 AS text)  ", "text", "45")
do_expr_test("e_expr-28.2.3", " CAST (-45 AS text) ", "text", "-45")
do_expr_test("e_expr-28.2.4", " CAST (8.8 AS text)    ", "text", "8.8")
do_expr_test("e_expr-28.2.5", " CAST (2.3e+5 AS text) ", "text", "230000.0")
do_expr_test("e_expr-28.2.6", " CAST (-2.3e-5 AS text) ", "text", "-2.3e-05")
do_expr_test("e_expr-28.2.7", " CAST (0.0 AS text) ", "text", "0.0")
do_expr_test("e_expr-28.2.7", " CAST (0 AS text) ", "text", "0")
-- EVIDENCE-OF: R-26346-36443 When casting a BLOB value to a REAL, the
-- value is first converted to TEXT.
--
do_expr_test("e_expr-29.1.1", " CAST (X'312E3233' AS REAL) ", "real", 1.23)
do_expr_test("e_expr-29.1.2", " CAST (X'3233302E30' AS REAL) ", "real", 230.0)
do_expr_test("e_expr-29.1.3", " CAST (X'2D392E3837' AS REAL) ", "real", -9.87)
do_expr_test("e_expr-29.1.4", " CAST (X'302E30303031' AS REAL) ", "real", 0.0001)

-- EVIDENCE-OF: R-54898-34554 When casting a TEXT value to REAL, the
-- longest possible prefix of the value that can be interpreted as a real
-- number is extracted from the TEXT value and the remainder ignored.
--
do_expr_test("e_expr-29.2.1", " CAST('1.23abcd' AS REAL) ", "real", 1.23)
do_expr_test("e_expr-29.2.2", " CAST('1.45.23abcd' AS REAL) ", "real", 1.45)
do_expr_test("e_expr-29.2.3", " CAST('-2.12e-01ABC' AS REAL) ", "real", -0.212)
do_expr_test("e_expr-29.2.4", " CAST('1 2 3 4' AS REAL) ", "real", 1.0)
-- EVIDENCE-OF: R-11321-47427 Any leading spaces in the TEXT value are
-- ignored when converging from TEXT to REAL.
--
do_expr_test("e_expr-29.3.1", " CAST(' 1.23abcd' AS REAL) ", "real", 1.23)
do_expr_test("e_expr-29.3.2", " CAST('    1.45.23abcd' AS REAL) ", "real", 1.45)
do_expr_test("e_expr-29.3.3", " CAST('   -2.12e-01ABC' AS REAL) ", "real", -0.212)
do_expr_test("e_expr-29.3.4", " CAST(' 1 2 3 4' AS REAL) ", "real", 1.0)
-- EVIDENCE-OF: R-22662-28218 If there is no prefix that can be
-- interpreted as a real number, the result of the conversion is 0.0.
--
do_expr_test("e_expr-29.4.1", " CAST('' AS REAL) ", "real", 0.0)
do_expr_test("e_expr-29.4.2", " CAST('not a number' AS REAL) ", "real", 0.0)
do_expr_test("e_expr-29.4.3", " CAST('XXI' AS REAL) ", "real", 0.0)
-- EVIDENCE-OF: R-21829-14563 When casting a BLOB value to INTEGER, the
-- value is first converted to TEXT.
--
do_expr_test("e_expr-30.1.1", " CAST(X'313233' AS INTEGER) ", "integer", 123)
do_expr_test("e_expr-30.1.2", " CAST(X'2D363738' AS INTEGER) ", "integer", -678)
do_expr_test("e_expr-30.1.3", [[
  CAST(X'31303030303030' AS INTEGER)
]], "integer", 1000000)
do_expr_test("e_expr-30.1.4", [[
  CAST(X'2D31313235383939393036383432363234' AS INTEGER)
]], "integer", -1125899906842624LL)

-- EVIDENCE-OF: R-47612-45842 When casting a TEXT value to INTEGER, the
-- longest possible prefix of the value that can be interpreted as an
-- integer number is extracted from the TEXT value and the remainder
-- ignored.
--
do_expr_test("e_expr-30.2.1", " CAST('123abcd' AS INT) ", "integer", 123)
do_expr_test("e_expr-30.2.2", " CAST('14523abcd' AS INT) ", "integer", 14523)
do_expr_test("e_expr-30.2.3", " CAST('-2.12e-01ABC' AS INT) ", "integer", -2)
do_expr_test("e_expr-30.2.4", " CAST('1 2 3 4' AS INT) ", "integer", 1)
-- EVIDENCE-OF: R-34400-33772 Any leading spaces in the TEXT value when
-- converting from TEXT to INTEGER are ignored.
--
do_expr_test("e_expr-30.3.1", " CAST('   123abcd' AS INT) ", "integer", 123)
do_expr_test("e_expr-30.3.2", " CAST('  14523abcd' AS INT) ", "integer", 14523)
do_expr_test("e_expr-30.3.3", " CAST(' -2.12e-01ABC' AS INT) ", "integer", -2)
do_expr_test("e_expr-30.3.4", " CAST('     1 2 3 4' AS INT) ", "integer", 1)
-- EVIDENCE-OF: R-43164-44276 If there is no prefix that can be
-- interpreted as an integer number, the result of the conversion is 0.
--
do_expr_test("e_expr-30.4.1", " CAST('' AS INTEGER) ", "integer", 0)
do_expr_test("e_expr-30.4.2", " CAST('not a number' AS INTEGER) ", "integer", 0)
do_expr_test("e_expr-30.4.3", " CAST('XXI' AS INTEGER) ", "integer", 0)
-- EVIDENCE-OF: R-08980-53124 The CAST operator understands decimal
-- integers only &mdash; conversion of hexadecimal integers stops at
-- the "x" in the "0x" prefix of the hexadecimal integer string and thus
-- result of the CAST is always zero.
do_expr_test("e_expr-30.5.1", " CAST('0x1234' AS INTEGER) ", "integer", 0)
do_expr_test("e_expr-30.5.2", " CAST('0X1234' AS INTEGER) ", "integer", 0)
-- EVIDENCE-OF: R-02752-50091 A cast of a REAL value into an INTEGER
-- results in the integer between the REAL value and zero that is closest
-- to the REAL value.
--
do_expr_test("e_expr-31.1.1", " CAST(3.14159 AS INTEGER) ", "integer", 3)
do_expr_test("e_expr-31.1.2", " CAST(1.99999 AS INTEGER) ", "integer", 1)
do_expr_test("e_expr-31.1.3", " CAST(-1.99999 AS INTEGER) ", "integer", -1)
do_expr_test("e_expr-31.1.4", " CAST(-0.99999 AS INTEGER) ", "integer", 0)
-- EVIDENCE-OF: R-51517-40824 If a REAL is greater than the greatest
-- possible signed integer (+9223372036854775807) then the result is the
-- greatest possible signed integer and if the REAL is less than the
-- least possible signed integer (-9223372036854775808) then the result
-- is the least possible signed integer.
--
do_expr_test("e_expr-31.2.1", " CAST(2e+50 AS INT) ", "integer", 9223372036854775807LL)
do_expr_test("e_expr-31.2.2", " CAST(-2e+50 AS INT) ", "integer", -9223372036854775808LL)
do_expr_test("e_expr-31.2.3", [[
  CAST(-9223372036854775809.0 AS INT)
]], "integer", -9223372036854775808LL)
do_expr_test("e_expr-31.2.4", [[
  CAST(9223372036854775809.0 AS INT)
]], "integer", 9223372036854775807LL)
-- EVIDENCE-OF: R-09295-61337 Casting a TEXT or BLOB value into NUMERIC
-- first does a forced conversion into REAL but then further converts the
-- result into INTEGER if and only if the conversion from REAL to INTEGER
-- is lossless and reversible.
--
do_expr_test("e_expr-32.1.1", " CAST('45'   AS NUMERIC)  ", "integer", 45)
do_expr_test("e_expr-32.1.2", " CAST('45.0' AS NUMERIC)  ", "integer", 45)
do_expr_test("e_expr-32.1.3", " CAST('45.2' AS NUMERIC)  ", "real", 45.2)
do_expr_test("e_expr-32.1.4", " CAST('11abc' AS NUMERIC) ", "integer", 11)
do_expr_test("e_expr-32.1.5", " CAST('11.1abc' AS NUMERIC) ", "real", 11.1)
-- EVIDENCE-OF: R-30347-18702 Casting a REAL or INTEGER value to NUMERIC
-- is a no-op, even if a real value could be losslessly converted to an
-- integer.
--
do_expr_test("e_expr-32.2.1", " CAST(13.0 AS NUMERIC) ", "real", 13.0)
do_expr_test("e_expr-32.2.2", " CAST(13.5 AS NUMERIC) ", "real", 13.5)
do_expr_test("e_expr-32.2.3", [[
  CAST(-9223372036854775808 AS NUMERIC)
]], "integer", -9223372036854775808LL)
do_expr_test("e_expr-32.2.4", [[
  CAST(9223372036854775807 AS NUMERIC)
]], "integer", 9223372036854775807LL)
-- EVIDENCE-OF: R-64550-29191 Note that the result from casting any
-- non-BLOB value into a BLOB and the result from casting any BLOB value
-- into a non-BLOB value may be different depending on whether the
-- database encoding is UTF-8
--
---------------------------------------------------------------------------
-- Test statements related to the EXISTS and NOT EXISTS operators.
--

--sqlite3("db", "test.db")
test:do_execsql_test(
    "e_expr-34.1",
    [[
        CREATE TABLE t1(id PRIMARY KEY, a, b);
        INSERT INTO t1 VALUES(1, 1, 2);
        INSERT INTO t1 VALUES(2, NULL, 2);
        INSERT INTO t1 VALUES(3, 1, NULL);
        INSERT INTO t1 VALUES(4, NULL, NULL);
    ]], {
        -- <e_expr-34.1>
        
        -- </e_expr-34.1>
    })

-- EVIDENCE-OF: R-25588-27181 The EXISTS operator always evaluates to one
-- of the integer values 0 and 1.
--
-- This statement is not tested by itself. Instead, all e_expr-34.* tests 
-- following this point explicitly test that specific invocations of EXISTS
-- return either integer 0 or integer 1.
--
-- EVIDENCE-OF: R-58553-63740 If executing the SELECT statement specified
-- as the right-hand operand of the EXISTS operator would return one or
-- more rows, then the EXISTS operator evaluates to 1.
--
local data = {
   {1, "EXISTS ( SELECT a FROM t1 )"},
   {2, "EXISTS ( SELECT b FROM t1 )"},
   {3, "EXISTS ( SELECT 24 )"},
   {4, "EXISTS ( SELECT NULL )"},
   {5, "EXISTS ( SELECT a FROM t1 WHERE a IS NULL )"},
}
for _, val in ipairs(data) do
    local tn = val[1]
    local expr = val[2]
    do_expr_test("e_expr-34.2."..tn, expr, "integer", 1)
end
-- EVIDENCE-OF: R-19673-40972 If executing the SELECT would return no
-- rows at all, then the EXISTS operator evaluates to 0.
--
data = {
    {1, "EXISTS ( SELECT a FROM t1 WHERE 0)"},
    {2, "EXISTS ( SELECT b FROM t1 WHERE a = 5)"},
    {3, "EXISTS ( SELECT 24 WHERE 0)"},
    {4, "EXISTS ( SELECT NULL WHERE 1=2)"},
}
for _, val in ipairs(data) do
    local tn = val[1]
    local expr = val[2]
    do_expr_test("e_expr-34.3."..tn, expr, "integer", 0)
end
-- EVIDENCE-OF: R-35109-49139 The number of columns in each row returned
-- by the SELECT statement (if any) and the specific values returned have
-- no effect on the results of the EXISTS operator.
    --
data = {
    {1, "EXISTS ( SELECT a,b FROM t1 )", 1},
    {2, "EXISTS ( SELECT a,b, a,b, a,b FROM t1 )", 1},
    {3, "EXISTS ( SELECT 24, 25 )", 1},
    {4, "EXISTS ( SELECT NULL, NULL, NULL )", 1},
    {5, "EXISTS ( SELECT a,b,a||b FROM t1 WHERE a IS NULL )", 1},
    {6, "EXISTS ( SELECT a, a FROM t1 WHERE 0)", 0},
    {7, "EXISTS ( SELECT b, b, a FROM t1 WHERE a = 5)", 0},
    {8, "EXISTS ( SELECT 24, 46, 89 WHERE 0)", 0},
    {9, "EXISTS ( SELECT NULL, NULL WHERE 1=2)", 0},
}
for _, val in ipairs(data) do
    local tn = val[1]
    local expr = val[2]
    local res = val[3]
    do_expr_test("e_expr-34.4."..tn, expr, "integer", res)
end
-- EVIDENCE-OF: R-10645-12439 In particular, rows containing NULL values
-- are not handled any differently from rows without NULL values.
--
data = {
    {1, "EXISTS (SELECT 'not null')", "EXISTS (SELECT NULL)"},
    {2, "EXISTS (SELECT NULL FROM t1)", "EXISTS (SELECT 'bread' FROM t1)"},
}

for _, val in ipairs(data) do
    local tn = val[1]
    local e1 = val[2]
    local e2 = val[3]
    local res = test:execsql("SELECT "..e1.."")[1   ]
    do_expr_test("e_expr-34.5."..tn.."a", e1, "integer", res)
    do_expr_test("e_expr-34.5."..tn.."b", e2, "integer", res)
end
---------------------------------------------------------------------------
-- Test statements related to scalar sub-queries.
--
-- catch { db close }
-- forcedelete test.db
-- sqlite3 db test.db
test:catchsql "DROP TABLE t22;"
test:do_execsql_test(
    "e_expr-35.0",
    [[
        CREATE TABLE t22(a PRIMARY KEY, b);
        INSERT INTO t22 VALUES('one', 'two');
        INSERT INTO t22 VALUES('three', NULL);
        INSERT INTO t22 VALUES(4, 5.0);
    ]], {
        -- <e_expr-35.0>
        
        -- </e_expr-35.0>
    })

-- EVIDENCE-OF: R-00980-39256 A SELECT statement enclosed in parentheses
-- may appear as a scalar quantity.
--
-- EVIDENCE-OF: R-56294-03966 All types of SELECT statement, including
-- aggregate and compound SELECT queries (queries with keywords like
-- UNION or EXCEPT) are allowed as scalar subqueries.
--
do_expr_test("e_expr-35.1.1", " (SELECT 35)   ", "integer", 35)
do_expr_test("e_expr-35.1.2", " (SELECT NULL) ", "null", "")
do_expr_test("e_expr-35.1.3", " (SELECT count(*) FROM t22) ", "integer", 3)
do_expr_test("e_expr-35.1.4", " (SELECT 4 FROM t22) ", "integer", 4)
do_expr_test("e_expr-35.1.5", [[ 
  (SELECT b FROM t22 UNION SELECT a+1 FROM t22)
]], "null", "")
do_expr_test("e_expr-35.1.6", [[ 
  (SELECT a FROM t22 UNION SELECT COALESCE(b, 55) FROM t22 ORDER BY 1)
]], "integer", 4)
-- EVIDENCE-OF: R-46899-53765 A SELECT used as a scalar quantity must
-- return a result set with a single column.
--
-- The following block tests that errors are returned in a bunch of cases
-- where a subquery returns more than one column.
--
data = {
    {1, "SELECT (SELECT * FROM t22 UNION SELECT a+1, b+1 FROM t22)"},
    {2, "SELECT (SELECT * FROM t22 UNION SELECT a+1, b+1 FROM t22 ORDER BY 1)"},
    {3, "SELECT (SELECT 1, 2)"},
    {4, "SELECT (SELECT NULL, NULL, NULL)"},
    {5, "SELECT (SELECT * FROM t22)"},
    {6, "SELECT (SELECT * FROM (SELECT 1, 2, 3))"},
}
local M = {1, "/sub--select returns [23] columns -- expected 1/"}
for _, val in ipairs(data) do
    local tn = val[1]
    local sql = val[2]
    test:do_catchsql_test(
        "e_expr-35.2."..tn,
        sql, M)

end
-- EVIDENCE-OF: R-35764-28041 The result of the expression is the value
-- of the only column in the first row returned by the SELECT statement.
--
-- EVIDENCE-OF: R-41898-06686 If the SELECT yields more than one result
-- row, all rows after the first are ignored.
--
test:do_execsql_test(
    "e_expr-36.3.1",
    [[
        CREATE TABLE t4(x PRIMARY KEY, y);
        INSERT INTO t4 VALUES(1, 'one');
        INSERT INTO t4 VALUES(2, 'two');
        INSERT INTO t4 VALUES(3, 'three');
    ]], {
        -- <e_expr-36.3.1>
        
        -- </e_expr-36.3.1>
    })

data = {
    {2, "( SELECT x FROM t4 ORDER BY x )     ", "integer", 1},
    {3, "( SELECT x FROM t4 ORDER BY y )     ", "integer", 1},
    {4, "( SELECT x FROM t4 ORDER BY x DESC )", "integer", 3},
    {5, "( SELECT x FROM t4 ORDER BY y DESC )", "integer", 2},
    {6, "( SELECT y FROM t4 ORDER BY y DESC )", "text", "two"},
    {7, "( SELECT sum(x) FROM t4 )          ", "integer", 6},
    {8, "( SELECT group_concat(y,'') FROM t4 )", "text", "onetwothree"},
    {9, "( SELECT max(x) FROM t4 WHERE y LIKE '___')", "integer", 2 },
}
for _, val in ipairs(data) do
    local tn = val[1]
    local expr = val[2]
    local restype = val[3]
    local resval = val[4]
    do_expr_test("e_expr-36.3."..tn, expr, restype, resval)
end
-- EVIDENCE-OF: R-25492-41572 If the SELECT yields no rows, then the
-- value of the expression is NULL.
--
data = {
    {1,  "( SELECT x FROM t4 WHERE x>3 ORDER BY x )"},
    {2,  "( SELECT x FROM t4 WHERE y<'one' ORDER BY y )"},
}
for _, val in ipairs(data) do
    local tn = val[1]
    local expr = val[2]
    do_expr_test("e_expr-36.4."..tn, expr, "null", "")
end
-- EVIDENCE-OF: R-62477-06476 For example, the values NULL, 0.0, 0,
-- 'english' and '0' are all considered to be false.
--
test:do_execsql_test(
    "e_expr-37.1",
    [[
        SELECT CASE WHEN NULL THEN 'true' ELSE 'false' END;
    ]], {
        -- <e_expr-37.1>
        "false"
        -- </e_expr-37.1>
    })

test:do_execsql_test(
    "e_expr-37.2",
    [[
        SELECT CASE WHEN 0.0 THEN 'true' ELSE 'false' END;
    ]], {
        -- <e_expr-37.2>
        "false"
        -- </e_expr-37.2>
    })

test:do_execsql_test(
    "e_expr-37.3",
    [[
        SELECT CASE WHEN 0 THEN 'true' ELSE 'false' END;
    ]], {
        -- <e_expr-37.3>
        "false"
        -- </e_expr-37.3>
    })

test:do_execsql_test(
    "e_expr-37.4",
    [[
        SELECT CASE WHEN 'engligh' THEN 'true' ELSE 'false' END;
    ]], {
        -- <e_expr-37.4>
        "false"
        -- </e_expr-37.4>
    })

test:do_execsql_test(
    "e_expr-37.5",
    [[
        SELECT CASE WHEN '0' THEN 'true' ELSE 'false' END;
    ]], {
        -- <e_expr-37.5>
        "false"
        -- </e_expr-37.5>
    })

-- EVIDENCE-OF: R-55532-10108 Values 1, 1.0, 0.1, -0.1 and '1english' are
-- considered to be true.
--
test:do_execsql_test(
    "e_expr-37.6",
    [[
        SELECT CASE WHEN 1 THEN 'true' ELSE 'false' END;
    ]], {
        -- <e_expr-37.6>
        "true"
        -- </e_expr-37.6>
    })

test:do_execsql_test(
    "e_expr-37.7",
    [[
        SELECT CASE WHEN 1.0 THEN 'true' ELSE 'false' END;
    ]], {
        -- <e_expr-37.7>
        "true"
        -- </e_expr-37.7>
    })

test:do_execsql_test(
    "e_expr-37.8",
    [[
        SELECT CASE WHEN 0.1 THEN 'true' ELSE 'false' END;
    ]], {
        -- <e_expr-37.8>
        "true"
        -- </e_expr-37.8>
    })

test:do_execsql_test(
    "e_expr-37.9",
    [[
        SELECT CASE WHEN -0.1 THEN 'true' ELSE 'false' END;
    ]], {
        -- <e_expr-37.9>
        "true"
        -- </e_expr-37.9>
    })

test:do_execsql_test(
    "e_expr-37.10",
    [[
        SELECT CASE WHEN '1english' THEN 'true' ELSE 'false' END;
    ]], {
        -- <e_expr-37.10>
        "true"
        -- </e_expr-37.10>
    })



test:finish_test()
