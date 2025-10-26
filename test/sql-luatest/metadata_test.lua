local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-4755', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

local g2 = t.group("full_metadata", {{remote = true}, {remote = false}})

g2.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g2.after_all(function(cg)
    cg.server:stop()
end)

g2.full_metadata = function(cg)
    cg.server:exec(function(remote)

        box.execute([[CREATE TABLE t (
            id INT PRIMARY KEY AUTOINCREMENT,
            a INT NOT NULL,
            c TEXT COLLATE "unicode_ci") WITH ENGINE = 'memtx';
        ]])

        box.execute("INSERT INTO t VALUES (1, 1, 'aSd');")

        local execute
        if remote then
            box.schema.user.create('metadata_test_user',
                                   {password = 'super_pass'})
            box.session.su('admin', box.schema.user.grant,
                           'metadata_test_user', 'read, write, execute',
                           'universe')
            box.session.su('admin', box.schema.user.grant,
                           'metadata_test_user', 'create', 'space')

            local netbox = require('net.box')
            cn = netbox.connect(box.cfg.listen, {user = 'metadata_test_user',
                                password = 'super_pass'})
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
        local exp_1 = {
            metadata = {
                 {
                    type = "string",
                    span = [['aSd' COLLATE "unicode_ci"]],
                    name =  "COLUMN_1",
                    collation =  "unicode_ci"
                 }
            },
            rows = {{'aSd'}},
        }
        t.assert_equals(execute("SELECT 'aSd' COLLATE \"unicode_ci\";"),
                        exp_1)

        local exp_2 = {
            metadata = {
                 {
                    span = "c",
                    type = "string",
                    is_nullable =  true,
                    name =  "c",
                    collation =  "unicode_ci"
                 }
            },
            rows = {{'aSd'}},
        }
        t.assert_equals(execute("SELECT c FROM t;"), exp_2)

        local exp_3 = {
            metadata = {
                 {
                    type = "string",
                    span = 'c COLLATE "unicode"',
                    name =  "COLUMN_1",
                    collation =  "unicode"
                 }
            },
            rows = {{'aSd'}},
        }
        t.assert_equals(execute("SELECT c COLLATE \"unicode\" FROM t;"),
                        exp_3)

        -- Make sure that nullability/autoincrement are presented.
        --
        local exp_4_5 = {
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
                 }
            },
            rows = {{1, 1, 'aSd'}},
        }
        t.assert_equals(execute("SELECT id, a, c FROM t;"), exp_4_5)
        t.assert_equals(execute("SELECT * FROM t;"), exp_4_5)

        -- Span is always set in extended metadata. Span is an original
        -- expression forming result set column.
        --
        local exp_6 = {
            metadata = {
                 {
                    type = "integer",
                    span = "1",
                    name =  "x",
                 }
            },
            rows = {{1}},
        }
        t.assert_equals(execute("SELECT 1 AS x;"), exp_6)

        local exp_7 = {
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
                }
            },
            rows = {{1, 1, 'aSd', 2, 1, 'aSdabc'}},
        }
        t.assert_equals(execute("SELECT *, id + 1 AS x, a AS y,\z
                                 c || 'abc' FROM t;"), exp_7)

        box.execute("CREATE VIEW v AS SELECT id + 1 AS x, a AS y,\z
                     c || 'abc' FROM t;")

        local exp_8 = {
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
                 }
            },
            rows = {{2, 1, 'aSdabc'}},
        }
        t.assert_equals(execute("SELECT * FROM v;"), exp_8)

        local exp_9 = {
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
                 }
            },
            rows = {{2, 1}},
        }
        t.assert_equals(execute("SELECT x, y FROM v;"), exp_9)

        execute([[SET SESSION "sql_full_metadata" = false;]])
        execute([[SET SESSION "sql_seq_scan" = false;]])
        box.space.v:drop()
        box.space.t:drop()

        t.assert_equals(box.space.t, nil)
        t.assert_equals(box.space.v, nil)

        if remote then
            cn:close()

            box.schema.user.revoke('metadata_test_user',
                                   'read, write, execute', 'universe')
            box.schema.user.revoke('metadata_test_user', 'create', 'space')
            box.schema.user.drop('metadata_test_user')
        end;
    end, {cg.params.remote})
end

--
-- gh-4755: collation in metadata must be displayed for both
-- string and scalar field types.
--
g.test_4755_scalar_collation_metadata = function(cg)
    cg.server:exec(function(engine)

        box.execute([[SET SESSION "sql_full_metadata" = true;]])
        box.execute([[SET SESSION "sql_seq_scan" = true;]])

        -- Create tables.
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
                }
            },
            rows = {},
        }

        t.assert_equals(box.execute("SELECT * FROM t;"), exp)

        box.execute([[SET SESSION "sql_full_metadata" = false;]])
        box.execute([[DROP TABLE t;]])
        t.assert_equals(box.space.t, nil)
    end, {cg.params.engine})
end
