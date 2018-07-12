#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(3)
local fiber = require("fiber")
local N = 20

-- this test uses ddl which is not working concurrently
-- see issue #2741
ch = fiber.channel(N)
for id = 1, N do
    fiber.create(
        function ()
            local table_name = "table2723"..id
            box.sql.execute("create table "..table_name.."(id INT primary key, a integer unique, b INT)")
            box.sql.execute("insert into "..table_name.." values(1, 2, 3)")
            box.sql.execute("insert into "..table_name.." values(3, 4, 3)")
            pcall( function() box.sql.execute("insert into "..table_name.." values(3, 4, 3)") end)
            box.sql.execute("drop table "..table_name)
            ch:put(1)
        end
    )
end
for id = 1, N do
    ch:get()
end

test:do_test(
    "concurrency:1",
    function()
        return test:execsql([[select count(*) from "_space" where "name" like 'table-2723-%']])[1]
    end,
    0)

ch = fiber.channel(N)
box.sql.execute("create table t1(id INT primary key, a integer unique, b INT);")
box.sql.execute("create index i1 on t1(b);")
for id = 1, N do
    fiber.create(
        function ()
            box.sql.execute(string.format("insert into t1 values(%s, %s, 3)", id, id))
            box.sql.execute(string.format("insert into t1 values(%s, %s, 3)", id+N, id+N))
            box.sql.execute(string.format("delete from t1 where id = %s", id+N))
            box.sql.execute(string.format("insert into t1 values(%s, %s, 3)", id+2*N, id+2*N))
            box.sql.execute(string.format("delete from t1 where id = %s", id+2*N))
            ch:put(1)
        end
    )
end
for id = 1, N do
    ch:get()
end
test:do_test(
    "concurrency:2",
    function()
        return test:execsql("select count(*) from (select distinct * from t1);")[1]
    end,
    N)
box.sql.execute("drop table t1;")


ch = fiber.channel(N)
box.sql.execute("create table t1(id INT primary key, a integer unique, b INT);")
box.sql.execute("create index i1 on t1(b);")
for id = 1, N*N do
    box.sql.execute(string.format("insert into t1 values(%s, %s, 3)", id, id))
end
for id = 1, N do
    fiber.create(
        function ()
            box.sql.execute("delete from t1")
            ch:put(1)
        end
    )
end
for id = 1, N do
    ch:get()
end
test:do_test(
    "concurrency:3",
    function()
        return test:execsql("select count(*) from t1;")[1]
    end,
    0)
box.sql.execute("drop table t1;")

test:finish_test()
