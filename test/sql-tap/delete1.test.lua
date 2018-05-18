#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(9)

--!./tcltestrunner.lua
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:execsql " DROP TABLE IF EXISTS test1 "
test:execsql " CREATE TABLE test1 (id INT, PRIMARY KEY (id)) "
test:execsql " INSERT INTO test1 values (1)"
test:execsql " INSERT INTO test1 values (2)"
test:execsql " INSERT INTO test1 values (3)"
test:execsql " DROP TABLE IF EXISTS test2 "
test:execsql " CREATE TABLE test2 (id INT, name TEXT, surname TEXT, bar INT, foo INT, qwerty INT, PRIMARY KEY (id)) "
test:execsql " CREATE INDEX test2_secondary ON test2 (id, name) "
test:execsql " CREATE INDEX test2_third ON test2 (surname, bar) "
test:execsql " CREATE INDEX test2_fourth ON test2 (qwerty) "
test:execsql [[ INSERT INTO test2 values (1, 'Vlad', 'Shpilevoy', 100, 200, 300) ]]
test:execsql [[ INSERT INTO test2 values (2, 'Ivan', 'Petrov', 200, 300, 400) ]]
test:execsql [[ INSERT INTO test2 values (3, 'Maria', 'Popova', 300, 400, 500) ]]
test:execsql [[ INSERT INTO test2 values (4, 'Albert', 'Sukaev', 400, 500, 600) ]]
test:execsql [[ INSERT INTO test2 values (5, 'Ksenia', 'Ivanova', 100, 200, 700) ]]
test:execsql [[ INSERT INTO test2 values (6, 'Brian', 'Hankok', 200, 300, 800) ]]
test:execsql " DROP TABLE IF EXISTS test3 "
test:execsql " CREATE TABLE test3 (id INT, name TEXT, surname TEXT, bar INT, foo INT, qwerty INT, PRIMARY KEY (id)) "
test:execsql " CREATE INDEX test3_secondary ON test3 (id, name) "
test:execsql " CREATE INDEX test3_third ON test3 (surname, bar) "
test:execsql " CREATE INDEX test3_fourth ON test3 (qwerty) "
test:execsql [[ INSERT INTO test3 values (1, 'Vlad', 'Shpilevoy', 100, 200, 300) ]]
test:execsql [[ INSERT INTO test3 values (2, 'Ivan', 'Petrov', 200, 300, 400) ]]
test:execsql [[ INSERT INTO test3 values (3, 'Maria', 'Popova', 300, 400, 500) ]]
test:execsql [[ INSERT INTO test3 values (4, 'Albert', 'Sukaev', 400, 500, 600) ]]
test:execsql [[ INSERT INTO test3 values (5, 'Ksenia', 'Ivanova', 100, 200, 700) ]]
test:execsql [[ INSERT INTO test3 values (6, 'Brian', 'Hankok', 200, 300, 800) ]]
test:execsql " DROP TABLE IF EXISTS test4 "
test:execsql " CREATE TABLE test4 (id INT, name TEXT, surname TEXT, bar INT, foo INT, qwerty INT, PRIMARY KEY (id)) "
test:execsql " CREATE INDEX test4_secondary ON test4 (id, name) "
test:execsql " CREATE INDEX test4_third ON test4 (surname, bar) "
test:execsql " CREATE INDEX test4_fourth ON test4 (qwerty) "
test:execsql [[ INSERT INTO test4 values (1, 'Vlad', 'Shpilevoy', 100, 200, 300) ]]
test:execsql [[ INSERT INTO test4 values (2, 'Ivan', 'Petrov', 200, 300, 400) ]]
test:execsql [[ INSERT INTO test4 values (3, 'Maria', 'Popova', 300, 400, 500) ]]
test:execsql [[ INSERT INTO test4 values (4, 'Albert', 'Sukaev', 400, 500, 600) ]]
test:execsql [[ INSERT INTO test4 values (5, 'Ksenia', 'Ivanova', 100, 200, 700) ]]
test:execsql [[ INSERT INTO test4 values (6, 'Brian', 'Hankok', 200, 300, 800) ]]
test:execsql " DROP TABLE IF EXISTS test5 "
test:execsql " CREATE TABLE test5 (id INT, name TEXT, surname TEXT, bar INT, foo INT, qwerty INT, PRIMARY KEY (id)) "
test:execsql " CREATE INDEX test5_secondary ON test5 (id, name) "
test:execsql " CREATE INDEX test5_third ON test5 (surname, bar) "
test:execsql " CREATE INDEX test5_fourth ON test5 (qwerty) "
test:execsql [[ INSERT INTO test5 values (1, 'Vlad', 'Shpilevoy', 100, 200, 300) ]]
test:execsql [[ INSERT INTO test5 values (2, 'Ivan', 'Petrov', 200, 300, 400) ]]
test:execsql [[ INSERT INTO test5 values (3, 'Maria', 'Popova', 300, 400, 500) ]]
test:execsql [[ INSERT INTO test5 values (4, 'Albert', 'Sukaev', 400, 500, 600) ]]
test:execsql [[ INSERT INTO test5 values (5, 'Ksenia', 'Ivanova', 100, 200, 700) ]]
test:execsql [[ INSERT INTO test5 values (6, 'Brian', 'Hankok', 200, 300, 800) ]]
test:do_test(
    "delete1-1.0",
    function()
        test:execsql "delete from test1 where id = 2"
        return test:execsql "select * from test1"
    end, {
        -- <delete1-1.0>
        1, 3
        -- </delete1-1.0>
    })

test:do_test(
    "delete1-2.0",
    function()
        test:execsql [[delete from test2 where name = 'Ivan']]
        return test:execsql "select name from test2"
    end, {
        -- <delete1-2.0>
        "Vlad", "Maria", "Albert", "Ksenia", "Brian"
        -- </delete1-2.0>
    })

test:do_test(
    "delete1-2.1",
    function()
        test:execsql "delete from test2 where id > 2"
        return test:execsql "select name from test2"
    end, {
        -- <delete1-2.1>
        "Vlad"
        -- </delete1-2.1>
    })

test:do_test(
    "delete1-3.0",
    function()
        test:execsql "delete from test3 where id >= 2 and id <= 5"
        return test:execsql "select name from test3"
    end, {
        -- <delete1-3.0>
        "Vlad", "Brian"
        -- </delete1-3.0>
    })

test:do_test(
    "delete1-3.1",
    function()
        test:execsql [[delete from test3 where surname = 'Hankok']]
        return test:execsql "select id from test3"
    end, {
        -- <delete1-3.1>
        1
        -- </delete1-3.1>
    })

test:do_test(
    "delete1-4.0",
    function()
        test:execsql "delete from test4 where foo >= 300"
        return test:execsql "select foo from test4"
    end, {
        -- <delete1-4.0>
        200, 200
        -- </delete1-4.0>
    })

test:do_test(
    "delete1-5.0",
    function()
        test:execsql [[delete from test5 where (foo > 300 or surname = 'Petrov')]]
        return test:execsql "select id from test5"
    end, {
        -- <delete1-5.0>
        1, 5, 6
        -- </delete1-5.0>
    })

-- Tests for data dictionary integration.
s = box.schema.create_space('t')
i = s:create_index('i', {parts={1, 'unsigned'}})
test:do_test(
    "delete1-6.0",
    function()
	s:replace({1})
	s:replace({2})
	s:replace({3})
	return s:count()
    end,
    3)

test:do_test(
    "delete1-6.1",
    function()
        box.sql.execute([[delete from "t"]])
	return s:count()
    end,
    0)

test:finish_test()

