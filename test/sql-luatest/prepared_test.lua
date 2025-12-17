local server = require('luatest.server')
local t = require('luatest')

local g = t.group("prepared", {{remote = true}, {remote = false}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_prepared = function(cg)
    cg.server:exec(function(remote)
        local cn
        local netbox = require('net.box')
        local execute
        local prepare
        local unprepare
        if remote then
            cn = netbox.connect(box.cfg.listen)
            execute = function(...) return cn:execute(...) end
            prepare = function(...) return cn:prepare(...) end
            unprepare = function(...) return cn:unprepare(...) end
        else
            execute = function(...)
                local res, err = box.execute(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
            prepare = function(...)
                local res, err = box.prepare(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
            unprepare = function(...)
                local res, err = box.unprepare(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
        end

        execute([[SET SESSION "sql_seq_scan" = true;]])
        -- Check default cache statistics.
        local exp = {
            cache = {
                size = 0,
                stmt_count = 0,
            },
        }
        local res = box.info.sql()
        t.assert_equals(res, exp)
        res = box.info:sql()
        t.assert_equals(res, exp)

        -- Test local interface and basic capabilities of prepared statements.
        execute('CREATE TABLE test (id INT PRIMARY KEY, a NUMBER, b TEXT);')
        local space = box.space.test
        space:replace{1, 2, '3'}
        space:replace{4, 5, '6'}
        space:replace{7, 8.5, '9'}
        local s, err = prepare("SELECT * FROM test WHERE id = ? AND a = ?;")
        t.assert_equals(err, nil)
        t.assert_equals(s.stmt_id, 3603193623)

        exp = {
            stmt_id = 3603193623,
            metadata = {
                {
                    name = "id",
                    type = "integer",
                },
                {
                    name = "a",
                    type = "number",
                },
                {
                    name = "b",
                    type = "string",
                },
            },
            params = {
                {
                    name = "?",
                    type = "ANY",
                },
                {
                    name = "?",
                    type = "ANY",
                },
            },
            param_count = 2,
        }
        t.assert_covers(s, exp)

        res = execute(s.stmt_id, {1, 2})
        t.assert_equals(res.rows, {{1, 2, '3'}})

        res = execute(s.stmt_id, {1, 3})
        t.assert_equals(res.rows, {})

        t.assert_not_equals(box.info.sql().cache.stmt_count, 0)
        t.assert_not_equals(box.info.sql().cache.size, 0)

        if not remote then
            res = s:execute({1, 2})
            t.assert_not_equals(res, nil)
            res = s:execute({1, 3})
            t.assert_not_equals(res, nil)
        end

        unprepare(s.stmt_id)

        t.assert_equals(box.info.sql().cache.stmt_count, 0)
        t.assert_equals(box.info.sql().cache.size, 0)

        -- Test preparation of different types of queries.
        -- Let's start from DDL. It doesn't make much sense since
        -- any prepared DDL statement can be executed once, but
        -- anyway make sure that no crashes occur.
        local function check_prepared_ddl_fails(sql)
            local s = prepare(sql)
            execute(s.stmt_id)
            local exp_err = {
                details = "statement has expired",
                message = "Failed to execute SQL statement: "..
                          "statement has expired",
                name = "SQL_EXECUTE",
            }
            t.assert_error_covers(exp_err, execute, s.stmt_id)
            unprepare(s.stmt_id)
        end

        check_prepared_ddl_fails("CREATE INDEX i1 ON test(a);")
        check_prepared_ddl_fails("DROP INDEX i1 ON test;")
        check_prepared_ddl_fails("CREATE VIEW v AS SELECT * FROM test;")
        check_prepared_ddl_fails("DROP VIEW v;")
        check_prepared_ddl_fails("ALTER TABLE test RENAME TO test1;")

        box.execute("CREATE TABLE test2 (id INT PRIMARY KEY);")
        local sql =  [[ALTER TABLE test2 ADD CONSTRAINT fk1
                       FOREIGN KEY (id) REFERENCES test2;]]
        check_prepared_ddl_fails(sql)
        box.space.test2:drop()
        t.assert_equals(box.space.test2, nil)

        sql = [[CREATE TRIGGER tr1 INSERT ON test1 FOR EACH ROW BEGIN
                DELETE FROM test1; END;]]
        check_prepared_ddl_fails(sql)
        check_prepared_ddl_fails("DROP TRIGGER tr1;")
        check_prepared_ddl_fails("DROP TABLE test1;")

        -- DQL
        execute('CREATE TABLE test (id INT PRIMARY KEY, a NUMBER, b TEXT);')
        space = box.space.test
        space:replace{1, 2, '3'}
        space:replace{4, 5, '6'}
        space:replace{7, 8.5, '9'}

        prepare("SELECT a FROM test WHERE b = '3';")
        s = prepare("SELECT a FROM test WHERE b = '3';")
        exp = {
            metadata = {
                {
                    name = "a",
                    type = "number",
                },
            },
            rows = {{2}},
        }
        res = execute(s.stmt_id)
        t.assert_equals(res, exp)
        res = execute(s.stmt_id)
        t.assert_equals(res, exp)

        if not remote then
            res = s:execute()
            t.assert_not_equals(res, nil)
            res = s:execute()
            t.assert_not_equals(res, nil)
        end

        unprepare(s.stmt_id)

        s = prepare("SELECT COUNT(*), COUNT(id - 3), MAX(b), ABS(id) "..
                    "FROM test WHERE b = '3';")

        res = execute(s.stmt_id)
        t.assert_equals(res.rows, {{1, 1, '3', 1}})
        res = execute(s.stmt_id)
        t.assert_equals(res.rows, {{1, 1, '3', 1}})
        unprepare(s.stmt_id)

        -- Let's try something a bit more complicated. For instance recursive
        -- query displaying Mandelbrot set.
        s = prepare([[WITH RECURSIVE
                          xaxis(x) AS (VALUES(-2.0) UNION ALL SELECT
                          x + 0.05 FROM xaxis WHERE x < 1.2),
                          yaxis(y) AS (VALUES(-1.0) UNION ALL SELECT
                          y + 0.1 FROM yaxis WHERE y < 1.0),
                          m(iter, cx, cy, x, y) AS (
                              SELECT 0, x, y, 0.0, 0.0 FROM xaxis, yaxis
                              UNION ALL
                              SELECT iter + 1, cx, cy, x * x - y * y + cx,
                              2.0 * x * y + cy FROM m
                                  WHERE (x * x + y * y) < 4.0 AND iter < 28),
                              m2(iter, cx, cy) AS (
                                  SELECT MAX(iter), cx, cy
                                  FROM m GROUP BY cx, cy),
                              a(t) AS (
                                  SELECT GROUP_CONCAT(SUBSTR(' .+*#',
                                  1 + LEAST(iter / 7,4), 1), '')
                                      FROM m2 GROUP BY cy)
                          SELECT GROUP_CONCAT(CAST(TRIM(TRAILING FROM t)
                          AS VARBINARY), x'0a') FROM a;]])

        res = execute(s.stmt_id)
        exp = {
            {
                name = "COLUMN_13",
                type = "varbinary",
            },
        }
        t.assert_equals(res.metadata, exp)
        unprepare(s.stmt_id)

        -- Workflow with bindings is still the same.
        s = prepare("SELECT a FROM test WHERE b = ?;")

        res = execute(s.stmt_id, {'6'})
        t.assert_equals(res.rows, {{5}})

        res = execute(s.stmt_id, {'9'})
        t.assert_equals(res.rows, {{8.5}})
        unprepare(s.stmt_id)
    end, {cg.params.remote})
end

-- gh-4760: make sure that names of all bindings are parsed correctly.
g.test_gh_4760_names_all_bindings_parsed_correctly = function(cg)
    cg.server:exec(function(remote)
        local cn
        local fiber = require('fiber')
        local netbox = require('net.box')
        local execute
        local prepare
        local unprepare
        if remote then
            cn = netbox.connect(box.cfg.listen)
            execute = function(...) return cn:execute(...) end
            prepare = function(...) return cn:prepare(...) end
            unprepare = function(...) return cn:unprepare(...) end
        else
            execute = function(...)
                local res, err = box.execute(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
            prepare = function(...)
                local res, err = box.prepare(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
            unprepare = function(...)
                local res, err = box.unprepare(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
        end

        execute([[SET SESSION "sql_seq_scan" = true;]])
        local s = prepare("SELECT a FROM test WHERE id = :id AND b = :name")
        local exp = {
            name = ":id",
            type = "ANY",
        }
        t.assert_equals(s.params[1], exp)
        exp = {
            name = ":name",
            type = "ANY",
        }
        t.assert_equals(s.params[2], exp)
        unprepare(s.stmt_id)

        s = prepare("SELECT ?, :id, :name, ?, @name2, ?")
        exp = {
            name = "?",
            type = "ANY",
        }
        t.assert_equals(s.params[1], exp)

        exp = {
            name = ":id",
            type = "ANY",
        }
        t.assert_equals(s.params[2], exp)

        exp = {
            name = ":name",
            type = "ANY",
        }
        t.assert_equals(s.params[3], exp)

        exp = {
            name = "?",
            type = "ANY",
        }
        t.assert_equals(s.params[4], exp)

        exp = {
            name = "@name2",
            type = "ANY",
        }
        t.assert_equals(s.params[5], exp)

        exp = {
            name = "?",
            type = "ANY",
        }
        t.assert_equals(s.params[6], exp)
        unprepare(s.stmt_id)

        -- DML
        s = prepare("INSERT INTO test VALUES (?, ?, ?);")
        execute(s.stmt_id, {5, 6, '7'})
        execute(s.stmt_id, {6, 10, '7'})
        execute(s.stmt_id, {9, 11, '7'})
        unprepare(s.stmt_id)

        -- EXPLAIN works fine.
        local s1 = prepare("EXPLAIN SELECT a FROM test WHERE b = '3';")
        local res = execute(s1.stmt_id)
        exp = {
            {
                name = "addr",
                type = "integer",
            },
            {
                name = "opcode",
                type = "text",
            },
            {
                name = "p1",
                type = "integer",
            },
            {
                name = "p2",
                type = "integer",
            },
            {
                name = "p3",
                type = "integer",
            },
            {
                name = "p4",
                type = "text",
            },
            {
                name = "p5",
                type = "text",
            },
            {
                name = "comment",
                type = "text",
            },
        }
        t.assert_equals(res.metadata, exp)
        t.assert_not_equals(res.rows, nil)

        local sql = "EXPLAIN QUERY PLAN SELECT a FROM test WHERE b = '3';"
        local s2 = prepare(sql)
        res = execute(s2.stmt_id)
        exp = {
            {
                name = "selectid",
                type = "integer",
            },
            {
                name = "order",
                type = "integer",
            },
            {
                name = "from",
                type = "integer",
            },
            {
                name = "detail",
                type = "text",
            },
        }
        t.assert_equals(res.metadata, exp)
        t.assert_not_equals(res.rows, nil)

        unprepare(s2.stmt_id)
        unprepare(s1.stmt_id)

        -- Prepare call re-compiles statement if it is expired
        -- after schema change.
        s = prepare("SELECT a FROM test WHERE b = ?;")
        local sp = box.schema.create_space("s")
        sp:drop()
        local exp_err = {
            details = "statement has expired",
            message = "Failed to execute SQL statement: statement has expired",
            name = "SQL_EXECUTE",
        }
        t.assert_error_covers(exp_err, execute, s.stmt_id)

        prepare("SELECT a FROM test WHERE b = ?;")
        exp = {
            metadata = {
                {
                    name = "a",
                    type = "number",
                },
            },
            rows = {},
        }
        res = execute(s.stmt_id)
        t.assert_equals(res, exp)
        unprepare(s.stmt_id)

        -- Setting cache size to 0 is possible only in case if
        -- there's no any prepared statements right now.
        box.cfg{sql_cache_size = 0}
        t.assert_equals(box.info.sql().cache.stmt_count, 0)
        t.assert_equals(box.info.sql().cache.size, 0)
        exp_err = {
            details = "Memory limit for SQL prepared statements "..
                      "has been reached. Please, deallocate active "..
                      "statements or increase SQL cache size.",
            message = "Failed to prepare SQL statement: "..
                      "Memory limit for SQL prepared statements "..
                      "has been reached. "..
                      "Please, deallocate active statements or "..
                      "increase SQL cache size.",
            name = "SQL_PREPARE",
        }
        t.assert_error_covers(exp_err, prepare, "SELECT a FROM test;")

        -- Still with small size everything should work.
        box.cfg{sql_cache_size = 1500}

        local ok = nil
        res = nil
        fiber.create(function()
            local sql = "SELECT * FROM SEQSCAN test WHERE a = %d;"
            for i = 1, 5 do
                pcall(prepare, string.format(sql, i))
            end
            sql = "SELECT * FROM SEQSCAN test WHERE b = '6';"
            ok, res = pcall(prepare, sql)
        end)

        t.helpers.retrying({}, function() t.assert_equals(ok, false) end)
        exp_err = "Failed to prepare SQL statement: "..
                  "Memory limit for SQL prepared statements has been "..
                  "reached. Please, deallocate active statements "..
                  "or increase SQL cache size."
        t.assert_equals(tostring(res), exp_err)

        -- Check that after fiber is dead, its session gets rid of
        -- all prepared statements.
        if remote then
            cn:close()
            cn = netbox.connect(box.cfg.listen)
        end
        box.cfg{sql_cache_size = 0}
        box.cfg{sql_cache_size = 3000}

        -- Make sure that if prepared statement is busy (is executed
        -- right now), prepared statement is not used, i.e. statement
        -- is compiled from scratch, executed and finilized.
        box.schema.func.create('SLEEP', {
            language = 'Lua',
            body = "function () require('fiber').sleep(0.3) return 1 end",
            exports = {'LUA', 'SQL'
        }});

        s = prepare("SELECT id, SLEEP() FROM SEQSCAN test;");
        t.assert_not_equals(s, nil);

        local function implicit_yield()
            s = prepare("SELECT id, SLEEP() FROM SEQSCAN test;")
            execute(s.stmt_id)
        end

        local f1 = fiber.new(implicit_yield)
        local f2 = fiber.new(implicit_yield)
        f1:set_joinable(true)
        f2:set_joinable(true)

        f1:join()
        res = f2:join()
        t.assert_equals(res, true)

        unprepare(s.stmt_id)

        -- Now during execution of one prepared statement, in another
        -- session schema is invalidated and statement is re-compiled.
        local function invalidate_schema_and_prepare()
            sp = box.schema.create_space("s2")
            sp:drop()
            s2 = prepare("SELECT id, SLEEP() FROM SEQSCAN test;")
            t.assert_not_equals(s2, nil)
            unprepare(s2.stmt_id)
        end

        f1 = fiber.new(implicit_yield)
        f2 = fiber.new(invalidate_schema_and_prepare)
        f1:set_joinable(true)
        f2:set_joinable(true)

        f1:join();
        res = f2:join()
        t.assert_equals(res, true)
    end, {cg.params.remote})
end

-- gh-4825: make sure that values to be bound are erased after
-- execution, so that they don't appear in the next statement
-- execution.
g.test_gh_4825_values_erased_after_execution = function(cg)
    cg.server:exec(function(remote)
        local cn
        local netbox = require('net.box')
        local execute
        local prepare
        local unprepare
        if remote then
            cn = netbox.connect(box.cfg.listen)
            execute = function(...) return cn:execute(...) end
            prepare = function(...) return cn:prepare(...) end
            unprepare = function(...) return cn:unprepare(...) end
        else
            execute = function(...)
                local res, err = box.execute(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
            prepare = function(...)
                local res, err = box.prepare(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
            unprepare = function(...)
                local res, err = box.unprepare(...)
                if err ~= nil then
                    error(err)
                end
                return res
            end
        end

        local s = prepare('SELECT :a, :b, :c');
        local res = execute(s.stmt_id, {{[':a'] = 1}, {[':b'] = 2},
                                       {[':c'] = 3}});
        t.assert_equals(res.rows, {{1, 2, 3}})
        res = execute(s.stmt_id, {{[':a'] = 1}, {[':b'] = 2}});
        t.assert_equals(res.rows, {{1, 2, nil}})
        res = execute(s.stmt_id)
        t.assert_equals(res.rows, {{nil, nil, nil}})
        unprepare(s.stmt_id)

        if remote then
            cn:close()
        end

        box.cfg{sql_cache_size = 5 * 1024 * 1024}
        box.space.test:drop()
        t.assert_equals(box.space.test, nil)
        box.schema.func.drop('SLEEP')
    end, {cg.params.remote})
end
