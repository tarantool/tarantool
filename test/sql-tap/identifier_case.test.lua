#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(73)

local test_prefix = "identifier_case-"

local data = {
    { 1,  [[ table1 ]], {0} },
    { 2,  [[ Table1 ]], {"/already exists/"} },
    { 3,  [[ TABLE1 ]], {"/already exists/"} },
    { 4,  [[ "TABLE1" ]], {"/already exists/"} },
    { 5,  [[ "table1" ]], {0} },
    { 6,  [[ "Table1" ]], {0} },
    -- non ASCII characters case is not supported
    { 7,  [[ русский ]], {0} },
    { 8,  [[ "русский" ]], {0} },
    { 9,  [[ Großschreibweise ]], {0} },
    { 10,  [[ Русский ]], {"/already exists/"} },
    { 11,  [[ Grossschreibweise ]], {"/already exists/"} },
}

for _, row in ipairs(data) do
    test:do_catchsql_test(
        test_prefix.."1.1."..row[1],
        string.format( [[
                CREATE TABLE %s (a int PRIMARY KEY);
                INSERT INTO %s values (%s);
                ]], row[2], row[2], row[1]),
        row[3])
end

data = {
    { 1, [[ table1 ]], {1}},
    { 2, [[ Table1 ]], {1}},
    { 3, [[ TABLE1 ]], {1}},
    { 4, [[ "TABLE1" ]], {1}},
    { 5, [[ "table1" ]], {5}},
    { 6, [[ "Table1" ]], {6}},
    { 7, [[ русский ]], {7}},
    { 8, [[ "русский" ]], {8}},
}

for _, row in ipairs(data) do
    test:do_execsql_test(
        test_prefix.."1.2."..row[1],
        string.format([[
            SELECT * FROM %s;
            ]], row[2]),
        row[3])
end

test:do_test(
    test_prefix.."1.3.1",
    function ()
        test:execsql([[ DROP TABLE table1; ]])
    end,
    nil)

test:do_test(
    test_prefix.."1.3.2",
    function ()
        test:execsql([[ DROP TABLE "table1"; ]])
    end,
    nil)

test:do_test(
    test_prefix.."1.3.3",
    function ()
        return test:drop_all_tables()
    end,
    4)

data = {
    { 1,  [[ columnn ]], {0} },
    { 2,  [[ Columnn ]], {0} },
    { 3,  [[ COLUMNN ]], {0} },
    { 4,  [[ "COLUMNN" ]], {0} },
    { 5,  [[ "columnn" ]], {0} },
    { 6,  [[ "Columnn" ]], {0} },
    { 7,  [[ "columNN" ]], {1, "Space field 'columNN' is duplicate"} }
}

for _, row in ipairs(data) do
    test:do_catchsql_test(
        test_prefix.."2.1."..row[1],
        string.format( [[
                CREATE TABLE table%s ("columNN" INT, %s INT, primary key("columNN", %s));
                INSERT INTO table%s(%s, "columNN") values (%s, %s);
                ]],
                row[1], row[2], row[2],
                row[1], row[2], row[1], row[1]+1),
        row[3])
end


data = {
    { 1,  [[ columnn ]], },
    { 2,  [[ Columnn ]], },
    { 3,  [[ COLUMNN ]], },
    { 4,  [[ "COLUMNN" ]], },
    { 5,  [[ "columnn" ]], },
    { 6,  [[ "Columnn" ]], }
}

for _, row in ipairs(data) do
    test:do_execsql_test(
        test_prefix.."2.2."..row[1],
        string.format([[
            SELECT %s FROM table%s;
            ]], row[2], row[1]),
        {row[1]})
end

test:do_test(
    test_prefix.."2.3.1",
    function ()
        return test:drop_all_tables()
    end,
    6)

test:execsql([[create table table1(pk INT PRIMARY KEY AUTOINCREMENT, columnn INT , "columnn" INT UNIQUE)]])
test:execsql([[insert into table1("columnn", "COLUMNN") values(2,1)]])


data = {
    --tn  lookup_cln    lookup_val  set_cln       set_val result
    { 1,  [[ columnn ]], 1,          [[ columnn ]], 3 ,     {3,2} },
    { 2,  [[ Columnn ]], 3,          [[ columnn ]], 4 ,     {4,2} },
    { 3,  [[ columnn ]], 4,          [[ Columnn ]], 5 ,     {5,2} },
    { 4,  [["COLUMNN"]], 5,          [[ columnn ]], 6 ,     {6,2} },
    { 5,  [[ columnn ]], 6,          [["COLUMNN"]], 7 ,     {7,2} },
    { 6,  [["columnn"]], 2,          [["columnn"]], 3 ,     {7,3} },
    { 7,  [["columnn"]], 3,          [[ columnn ]], 8 ,     {8,3} },
    { 8,  [["columnn"]], 8,          [[ columnn ]], 1000 ,  {8,3} },
}

for _, row in ipairs(data) do
    local tn = row[1]
    local lookup_cln = row[2]
    local lookup_val = row[3]
    local set_cln = row[4]
    local set_val = row[5]
    local result = row[6]
    test:do_execsql_test(
        test_prefix.."3.1."..tn,
        string.format( [[
                UPDATE table1 set %s = %s where %s = %s;
                SELECT table1.columnn, "TABLE1"."columnn" FROM table1;
                ]],
            set_cln, set_val, lookup_cln, lookup_val),
        result)
end

test:do_test(
    test_prefix.."3.2.1",
    function ()
        return test:drop_all_tables()
    end,
    1)

test:do_execsql_test(
    test_prefix.."4.0",
    string.format([[create table table1(a INT , b  INT primary key)]]),
    nil
)

test:do_execsql_test(
    test_prefix.."4.1",
    string.format([[select * from table1 order by a]]),
    {}
)

data = {
    { 1,  [[ trigger1 ]], {0}},
    { 2,  [[ Trigger1 ]], {1, "Trigger 'TRIGGER1' already exists"}},
    { 3,  [["TRIGGER1"]], {1, "Trigger 'TRIGGER1' already exists"}},
    { 4,  [["trigger1" ]], {0}}
}

for _, row in ipairs(data) do
    test:do_catchsql_test(
        test_prefix.."5.1."..row[1],
        string.format( [[
                CREATE TRIGGER %s DELETE ON table1 FOR EACH ROW BEGIN SELECT 1; END
                ]], row[2]),
        row[3])
end

data = {
    { 1,  [[ trigger1 ]], {0}},
    { 2,  [["trigger1" ]], {0}}
}

for _, row in ipairs(data) do
    test:do_catchsql_test(
        test_prefix.."5.2."..row[1],
        string.format( [[
                DROP TRIGGER %s
                ]], row[2]),
        row[3])
end


-- Check that collaiton names work as identifiers
data = {
    { 1,  [[ binary ]], {1, "Collation 'BINARY' does not exist"}},
    { 2,  [[ BINARY ]], {1, "Collation 'BINARY' does not exist"}},
    { 3,  [["binary"]], {0}},
    { 4,  [["bInaRy"]], {1, "Collation 'bInaRy' does not exist"}},
    { 5,  [["unicode"]], {0}},
    { 6,  [[ unicode ]], {1,"Collation 'UNICODE' does not exist"}},
    { 7,  [["UNICODE"]], {1,"Collation 'UNICODE' does not exist"}},
    { 8,  [[NONE]], {1,"Collation 'NONE' does not exist"}},
    { 9,  [["none"]], {0}},
}

test:do_catchsql_test(
    test_prefix.."6.0.",
    [[
        CREATE TABLE T1 (a TEXT primary key, b TEXT);
    ]],
    {0})

for _, row in ipairs(data) do
    test:do_catchsql_test(
        test_prefix.."6.1."..row[1],
        string.format( [[
                CREATE INDEX I%s ON T1 (b COLLATE %s);
                ]], row[1], row[2]),
        row[3])
end

test:do_catchsql_test(
    test_prefix.."6.2",
    [[
        DROP TABLE T1;
    ]],
    {0})

data = {
    { 1,  [[ 'a' < 'b' collate binary ]], {1, "Collation 'BINARY' does not exist"}},
    { 2,  [[ 'a' < 'b' collate "binary" ]], {0, {true}}},
    { 3,  [[ 'a' < 'b' collate 'binary' ]], {1, [[Syntax error at line 1 near ''binary'']]}},
    { 4,  [[ 'a' < 'b' collate "unicode" ]], {0, {true}}},
    { 5,  [[ 5 < 'b' collate "unicode" ]], {0, {true}}},
    { 6,  [[ 5 < 'b' collate unicode ]], {1,"Collation 'UNICODE' does not exist"}},
    { 7,  [[ 5 < 'b' collate "unicode_ci" ]], {0, {true}}},
    { 8,  [[ 5 < 'b' collate NONE ]], {1, "Collation 'NONE' does not exist"}},
    { 9,  [[ 5 < 'b' collate "none" ]], {0, {true}}},
}

for _, row in ipairs(data) do
    test:do_catchsql_test(
        test_prefix.."6.3."..row[1],
        string.format( [[
                SELECT %s;
                ]], row[2]),
        row[3])
end


test:finish_test()
