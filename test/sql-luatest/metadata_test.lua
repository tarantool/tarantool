local server = require('luatest.server')
local t = require('luatest')

local g = t.group("full_metadata", {{remote = true}, {remote = false}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_full_metadata = function(cg)
    cg.server:exec(function(remote)

        box.execute([[CREATE TABLE t (
            id INT PRIMARY KEY AUTOINCREMENT,
            a INT NOT NULL,
            c TEXT COLLATE "unicode_ci") WITH ENGINE = 'memtx';
        ]])

        box.execute("INSERT INTO t VALUES (1, 1, 'aSd');")

        local execute
        local cn
        if remote then
            local netbox = require('net.box')
            cn = netbox.connect(box.cfg.listen)
            execute = function(...) return cn:execute(...) end
        else
            execute = function(...)
                local res, err = box.execute(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
        end;

        execute([[SET SESSION "sql_seq_scan" = true;]])
        execute([[SET SESSION "sql_full_metadata" = true;]])

        -- Make sure collation is presented in extended metadata.
        --
        local exp = {
            metadata = {
                 {
                    type = "string",
                    span = [['aSd' COLLATE "unicode_ci"]],
                    name =  "COLUMN_1",
                    collation =  "unicode_ci"
                 },
            },
            rows = {{'aSd'}},
        }
        local res = execute([[SELECT 'aSd' COLLATE "unicode_ci";]])
        t.assert_equals(res, exp)

        exp = {
            metadata = {
                 {
                    span = "c",
                    type = "string",
                    is_nullable =  true,
                    name =  "c",
                    collation =  "unicode_ci"
                 },
            },
            rows = {{'aSd'}},
        }
        t.assert_equals(execute("SELECT c FROM t;"), exp)

        exp = {
            metadata = {
                 {
                    type = "string",
                    span = 'c COLLATE "unicode"',
                    name =  "COLUMN_1",
                    collation =  "unicode"
                 },
            },
            rows = {{'aSd'}},
        }
        res = execute([[SELECT c COLLATE "unicode" FROM t;]])
        t.assert_equals(res, exp)

        -- Make sure that nullability/autoincrement are presented.
        --
        exp = {
            metadata = {
                 {
                    span = "id",
                    type = "integer",
                    is_autoincrement = true,
                    name =  "id",
                    is_nullable = false
                 },
                 {
                    type = "integer",
                    span = "a",
                    name = "a",
                    is_nullable = false
                 },
                 {
                     span = "c",
                     type = "string",
                     is_nullable = true,
                     name = "c",
                     collation = "unicode_ci"
                 },
            },
            rows = {{1, 1, 'aSd'}},
        }
        t.assert_equals(execute("SELECT id, a, c FROM t;"), exp)
        t.assert_equals(execute("SELECT * FROM t;"), exp)

        -- Span is always set in extended metadata. Span is an original
        -- expression forming result set column.
        --
        exp = {
            metadata = {
                 {
                    type = "integer",
                    span = "1",
                    name =  "x",
                 },
            },
            rows = {{1}},
        }
        t.assert_equals(execute("SELECT 1 AS x;"), exp)

        exp = {
            metadata = {
                {
                    span = "id",
                    type = "integer",
                    is_autoincrement = true,
                    name = "id",
                    is_nullable = false
                },
                {
                    type = "integer",
                    span = "a",
                    name = "a",
                    is_nullable = false
                },
                {
                    span = "c",
                    type = "string",
                    is_nullable = true,
                    name = "c",
                    collation = "unicode_ci"
                },
                {
                    type = "integer",
                    span = "id + 1",
                    name = "x"
                },
                {
                    type = "integer",
                    span = "a",
                    name = "y",
                    is_nullable = false
                },
                {
                    type = "string",
                    span = "c || 'abc'",
                    name = "COLUMN_1"
                },
            },
            rows = {{1, 1, 'aSd', 2, 1, 'aSdabc'}},
        }
        local sql = "SELECT *, id + 1 AS x, a AS y, c || 'abc' FROM t;"
        t.assert_equals(execute(sql), exp)

        sql = "CREATE VIEW v AS SELECT id + 1 AS x, a AS y, c || 'abc' FROM t;"
        box.execute(sql)

        exp = {
            metadata = {
                 {
                    type = "integer",
                    span = "x",
                    name = "x"
                 },
                 {
                    type = "integer",
                    span = "y",
                    name = "y",
                    is_nullable = false
                 },
                 {
                     type = "string",
                     span = "COLUMN_1",
                     name = "COLUMN_1"
                 },
            },
            rows = {{2, 1, 'aSdabc'}},
        }
        t.assert_equals(execute("SELECT * FROM v;"), exp)

        exp = {
            metadata = {
                 {
                    type = "integer",
                    span = "x",
                    name = "x"
                 },
                 {
                    type = "integer",
                    span = "y",
                    name = "y",
                    is_nullable = false
                 },
            },
            rows = {{2, 1}},
        }
        t.assert_equals(execute("SELECT x, y FROM v;"), exp)

        execute([[SET SESSION "sql_full_metadata" = false;]])
        execute([[SET SESSION "sql_seq_scan" = false;]])
        box.space.v:drop()
        box.space.t:drop()

        t.assert_equals(box.space.t, nil)
        t.assert_equals(box.space.v, nil)

        if remote then
            cn:close()
        end
    end, {cg.params.remote})
end

g = t.group("gh-4755", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-4755: collation in metadata must be displayed for both
-- string and scalar field types.
--
g.test_4755_scalar_collation_metadata = function(cg)
    cg.server:exec(function(engine)

        box.execute([[SET SESSION "sql_full_metadata" = true;]])
        box.execute([[SET SESSION "sql_seq_scan" = true;]])

        local sql = string.format([[
            CREATE TABLE t (
                 a SCALAR COLLATE "unicode_ci" PRIMARY KEY,
                 b STRING COLLATE "unicode_ci"
            ) WITH ENGINE = '%s';
        ]], engine)
        box.execute(sql)

        local exp = {
            metadata = {
                {
                    span = "a",
                    type = "scalar",
                    is_nullable =  false,
                    name =  "a",
                    collation =  "unicode_ci"
                },
                {
                    span =  "b",
                    type =  "string",
                    is_nullable =  true,
                    name =  "b",
                    collation = "unicode_ci"
                },
            },
            rows = {},
        }

        t.assert_equals(box.execute("SELECT * FROM t;"), exp)

        box.execute([[SET SESSION "sql_full_metadata" = false;]])
        box.execute([[DROP TABLE t;]])
        t.assert_equals(box.space.t, nil)
    end, {cg.params.engine})
end
