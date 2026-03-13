local server = require('luatest.server')
local t = require('luatest')

local g = t.group("tokenize", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check errors during function create process
g.test_tokinizer = function(cg)
    cg.server:exec(function()
        package.path = os.getenv("BUILDDIR")..
                       "/test/sql-luatest/?.lua;"..
                       package.path
        local sql_tokenizer = require('sql_tokenizer')

        local exp = {
            "SELECT 1;",
            " SELECT 2;",
            " SELECT 3;",
        }
        local res = sql_tokenizer.split_sql('SELECT 1; SELECT 2; SELECT 3;')
        t.assert_equals(res, exp)

        t.assert_equals(sql_tokenizer.split_sql(';'), {})
        t.assert_equals(sql_tokenizer.split_sql(''), {})

        exp = {
            "SELECT 1",
        }
        t.assert_equals(sql_tokenizer.split_sql('SELECT 1'), exp)

        exp = {
            "CREATE TABLE test(a, b, c, d, PRIMARY KEY(a, b, c));",
        }
        local sql = 'CREATE TABLE test(a, b, c, d, PRIMARY KEY(a, b, c));'
        res = sql_tokenizer.split_sql(sql)
        t.assert_equals(res, exp)

        exp = {
            [[SELECT ";" FROM ";;;;"'' AS ";";]],
            [[ SELECT 100;]],
        }
        sql = 'SELECT ";" FROM ";;;;"\'\' AS ";"; SELECT 100;     \n\n'
        res = sql_tokenizer.split_sql(sql)
        t.assert_equals(res, exp)

        exp = {
            "CREATE TRIGGER BEGIN\n"..
            "SELECT 1; SELECT 2;\n"..
            "CREATE TABLE a (b,c,d);\n"..
            "END;",
        }
        res = sql_tokenizer.split_sql('CREATE TRIGGER BEGIN\n'..
                                      'SELECT 1; SELECT 2;\n'..
                                      'CREATE TABLE a (b,c,d);\n'..
                                      'END;'
                                      )
        t.assert_equals(res, exp)

        sql = '-- COMMENT COMMENT COMMENT; SELECT 1; SELECt 2;'
        t.assert_equals(sql_tokenizer.split_sql(sql), {})

        exp = {
            "SELECT 1;",
            " SELECT /* COMMENT COMMENT SELECT 100; SELECT 1000; */ 3;",
            " SELECT 2;",
        }
        sql = "SELECT 1; SELECT /* COMMENT COMMENT SELECT 100; "..
              "SELECT 1000; */ 3; SELECT 2;"
        res = sql_tokenizer.split_sql(sql)
        t.assert_equals(res, exp)

        sql = "SELECT abc FROM test WHERE CASE abc WHEN 100 "..
              "THEN kek ELSE grek END;"
        exp = {
            sql,
        }
        res = sql_tokenizer.split_sql(sql)
        t.assert_equals(res, exp)

        exp = {
            "CREATE temp TRIGGER BEGIN\n"..
            "SELECT CASE COND WHEN a 100 ELSE 200 END;\n"..
            "INSERT INTO tst VALUES v a l u e s;\n"..
            "END;",
            "\n\n\n\t\t\n\n SELECT 200..\";SELECT1000\";",
        }
        res = sql_tokenizer.split_sql('CREATE temp TRIGGER BEGIN\n'..
                                      'SELECT CASE COND WHEN a 100 '..
                                      'ELSE 200 END;\n'..
                                      'INSERT INTO tst VALUES v a l u e s;\n'..
                                      'END;\n\n\n\t\t\n\n SELECT 200..";'..
                                      'SELECT1000";'
                                      )
        t.assert_equals(res, exp)

        exp = {
            "SELECT 1;",
            " SELECT 2;",
        }
        res = sql_tokenizer.split_sql('SELECT 1; -- test COMMENT\n; SELECT 2;')
        t.assert_equals(res, exp)

        exp = {
            "START TRANSACTION;",
            " INSERT INTO table VALUES ('example kek--');",
            " COMMIT;",
        }
        sql = "START TRANSACTION; INSERT INTO table VALUES "..
              "('example kek--'); COMMIT;"
        res = sql_tokenizer.split_sql(sql)
        t.assert_equals(res, exp)

        exp = {
            "INSERT INTO quoted(a) VALUE 'lalala'",
        }
        res = sql_tokenizer.split_sql("INSERT INTO quoted(a) VALUE 'lalala'")
        t.assert_equals(res, exp)

        exp = {
            "INSERT INTO table VALUES"..
            "('textextextext''textextext''extext--extext/*ext''ext*/');",
            " COMMIT;",
        }
        res = sql_tokenizer.split_sql("INSERT INTO table VALUES"..
                                      "('textextextext''textextext''extext"..
                                      "--extext/*ext''ext*/'); "..
                                      "COMMIT;"
                                      )
        t.assert_equals(res, exp)
    end)
end
