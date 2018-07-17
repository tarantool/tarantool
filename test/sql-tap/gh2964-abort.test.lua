#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(90)

local test_prefix = "gh2964-abort-"

test:do_catchsql_test(
    test_prefix.."1.0.1",
    "CREATE TABLE t1 (a int primary key);")

test:do_catchsql_test(
    test_prefix.."1.0.2",
    "CREATE TABLE t2 (a int primary key);")

local insert_err = {1, "Duplicate key exists in unique index 'pk_unnamed_T2_1' in space 'T2'"}
local data = {
--id|TRIG TYPE|INSERT TYPE|insert error|commit error| result
 {1, "AFTER", "or abort",   insert_err, {0},          {1,1,2}},
 {2, "AFTER", "or rollback",insert_err, {1, "/no transaction is active/"}, {}},
 {3, "AFTER", "or fail",    insert_err, {0},          {1,2,1,2}},
 {4, "AFTER", "or ignore",  {0}       , {0},          {1,2,1,2}},
 {5, "BEFORE","or abort",   insert_err, {0},          {1,1,2}},
 {6, "BEFORE","or rollback",insert_err, {1, "/no transaction is active/"}, {}},
 {7, "BEFORE","or fail",    insert_err, {0},          {1,1,2}},
 {8, "BEFORE","or ignore",  {0}       , {0},          {1,2,1,2}}
}

for _, val in ipairs(data) do
    local ID = val[1]
    local TRIG_TYPE = val[2]
    local INSERT_TYPE = val[3]
    local INSERT_ERROR = val[4]
    local COMMIT_ERROR = val[5]
    local RESULT = val[6]
    local local_test_prefix = test_prefix.."1."..ID.."."
    test:do_catchsql_test(
        local_test_prefix.."0.3",
        string.format([[
        CREATE TRIGGER TRIG1 %s INSERT ON T1
        begin
            insert %s into t2 values(new.a);
        end;]], TRIG_TYPE, INSERT_TYPE),
        {0})

    test:do_catchsql_test(
        local_test_prefix.."1",
        "START TRANSACTION;")

    test:do_catchsql_test(
        local_test_prefix.."2",
        "insert into t1 values(1);")

    test:do_catchsql_test(
        local_test_prefix.."3",
        "insert into t2 values(2);")

    test:do_execsql_test(
        local_test_prefix.."4",
        "select * from t1 union all select * from t2;",
        {1, 1, 2})

    test:do_catchsql_test(
        local_test_prefix.."5",
        "insert into t1 values(2);",
        INSERT_ERROR)

    test:do_execsql_test(
        local_test_prefix.."6",
        "select * from t1 union all select * from t2;",
        RESULT)

    test:do_catchsql_test(
        local_test_prefix.."7",
        "commit;",
        COMMIT_ERROR)

    test:do_execsql_test(
        local_test_prefix.."8",
        "select * from t1 union all select * from t2;",
        RESULT)

    test:do_catchsql_test(
        local_test_prefix.."9",
        "delete from t1; delete from t2;",{0})

    test:do_catchsql_test(
        local_test_prefix.."10",
        "DROP TRIGGER TRIG1;",{0})
end

test:finish_test()
