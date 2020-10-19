#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(14)

test:do_test(
    "lua-tables-prepare-1",
    function()
        local format, s
        format = {}
        format[1] = { name = 'id', type = 'scalar'}
        format[2] = { name = 'f2', type = 'scalar'}
        s = box.schema.create_space('t', {format = format})
        s:create_index('i', {parts={1, 'scalar'}})

        s:replace{1, 4}
        s:replace{2, 2}
        s:replace{3, 3}
        s:replace{4, 3}

        local s1 = box.schema.create_space('t1')
        s1:create_index('i', {parts={1, 'scalar'}})
        s1:replace{1, 1}
    end,
    {})

test:do_execsql_test(
    "lua-tables-2",
    [[SELECT *
        FROM "t" as t1, "t" as t2
        WHERE t1."id" = t2."f2"
    ]],
    {4, 3, 1, 4, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 3})

test:do_execsql_test(
    "lua-tables-3",
    [[create view v as SELECT * FROM "t";
      select * from v;
    ]],
    {1, 4, 2, 2, 3, 3, 4, 3})

test:do_catchsql_test(
    "lua-tables-4",
    [[SELECT * from t1]],
    {1, "Space 'T1' does not exist"}
)

test:do_catchsql_test(
    "lua-tables-5",
    [[SELECT * from "t1"]],
    {1, "SQL does not support space without format"}
)
-- Extract from tkt3527.test.lua
test:do_test(
    "lua-tables-prepare-6",
    function()
        local format, s
        format = {{name = "CODE", type = "integer"},
            {name = "NAME", type = "scalar"}}
        s = box.schema.create_space("ELEMENT", {format = format})
        s:create_index('pk', {parts = {1, 'scalar'}})
        s:replace{1, 'Elem1'}
        s:replace{2, 'Elem2'}
        s:replace{3, 'Elem3'}
        s:replace{4, 'Elem4'}
        s:replace{5, 'Elem5'}

        format  = {{name = "CODEOR", type = "scalar"},
            {name = "CODE", type = "scalar"}}
        s = box.schema.create_space("ELEMOR", {format = format})
        s:create_index('pk', {parts = {1, 'scalar', 2, 'scalar'}})
        s:replace{3, 4}
        s:replace{4, 5}

        format = {{name = "CODEAND", type = "scalar"},
            {name = "CODE", type = "scalar"},
            {name = "ATTR1", type = "scalar"},
            {name = "ATTR2", type = "scalar"},
            {name = "ATTR3", type = "scalar"}}
        s = box.schema.create_space("ELEMAND", {format = format})
        s:create_index('pk', {parts = {1, "scalar", 2, "scalar"}})
        s:replace{1, 3, 'a', 'b', 'c'}
        s:replace{1, 2, 'x', 'y', 'z'}
    end,
    {})

test:do_execsql_test(
    "lua-tables-7",
    [[CREATE VIEW ElemView1 AS
      SELECT
        CAST(Element.Code AS VARCHAR(50)) AS ElemId,
       Element.Code AS ElemCode,
       Element.Name AS ElemName,
       ElemAnd.Code AS InnerCode,
       ElemAnd.Attr1 AS Attr1,
       ElemAnd.Attr2 AS Attr2,
       ElemAnd.Attr3 AS Attr3,
       0 AS Level,
       0 AS IsOrElem
      FROM Element JOIN ElemAnd ON ElemAnd.CodeAnd=Element.Code
      WHERE ElemAnd.CodeAnd NOT IN (SELECT CodeOr FROM ElemOr)
      UNION ALL
      SELECT
        CAST(ElemOr.CodeOr AS VARCHAR(50)) AS ElemId,
        Element.Code AS ElemCode,
        Element.Name AS ElemName,
        ElemOr.Code AS InnerCode,
        NULL AS Attr1,
        NULL AS Attr2,
        NULL AS Attr3,
        0 AS Level,
        1 AS IsOrElem
      FROM ElemOr JOIN Element ON Element.Code=ElemOr.CodeOr
      ORDER BY ElemId, InnerCode;]],
    {})

test:do_execsql_test(
    "lua-tables-8",
    [[SELECT * FROM ElemView1]],
    {"1",1,"Elem1",2,"x","y","z",0,0,
     "1",1,"Elem1",3,"a","b","c",0,0,
     "3",3,"Elem3",4,"","","",0,1,
     "4",4,"Elem4",5,"","","",0,1})

test:do_execsql_test(
    "lua-tables-9",
    [[SELECT * FROM "t" INDEXED BY "i"]],
    {1, 4, 2, 2, 3, 3, 4, 3})

-- gh-3886: indexes created from Lua are visible
-- to query optimizer.
--
test:do_test(
    "lua-tables-prepare-10",
    function()
        local sp = box.schema.space.create("TEST", {
            engine = 'memtx',
            format = {
                { name = 'ID', type = 'unsigned' },
                { name = 'A', type = 'unsigned' }
            }})
        sp:create_index('primary', {parts = {1, 'unsigned' } })
        sp:create_index('secondary', {parts = {2, 'unsigned' } })
        sp:insert({1,1})
        sp:insert({2,2})
        sp:insert({3,3})
    end,
    {})

test:do_eqp_test(
    11,
    [[
        SELECT * FROM test WHERE id = 2;
    ]], {
        {0, 0, 0, 'SEARCH TABLE TEST USING PRIMARY KEY (ID=?) (~1 row)'}
    })

test:do_eqp_test(
    12,
    [[
        SELECT * FROM test WHERE a = 5;
    ]], {
        {0, 0, 0, 'SEARCH TABLE TEST USING COVERING INDEX secondary (A=?) (~1 row)'}
    })

-- Make sure that without format it is impossible to create
-- an index: format is required to resolve column names.
test:do_test(
    "no-format-create-index-prep",
    function()
        box.schema.create_space('T')
    end, {})

test:do_catchsql_test(
    "no-format-create-index",
    [[
        CREATE INDEX i1 ON t(id);
    ]],
        {1, "SQL does not support space without format"})

test:finish_test()
