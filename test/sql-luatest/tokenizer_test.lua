local server = require('luatest.server')
local t = require('luatest')

local g = t.group("tokenize")

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_tokenizer = function(cg)
    cg.server:exec(function()
        package.path = os.getenv("BUILDDIR") .. "/test/sql-tap/lua/?.lua;" ..
                       package.path
        local sql_tokenizer = require('sql_tokenizer')

        local exp = {
            "select 1;",
            " select 2;",
            " select 3;",
        }
        local res = sql_tokenizer.split_sql('select 1; select 2; select 3;')
        t.assert_equals(res, exp)

        t.assert_equals(sql_tokenizer.split_sql(';'), {})
        t.assert_equals(sql_tokenizer.split_sql(''), {})

        exp = {
            'select 1',
        }
        t.assert_equals(sql_tokenizer.split_sql('select 1'), exp)

        exp = {
            'create table test(a, b, c, d, primary key(a, b, c))',
        }
        local sql = 'create table test(a, b, c, d, primary key(a, b, c))'
        res = sql_tokenizer.split_sql(sql)
        t.assert_equals(res, exp)

        exp = {
            [[select ";" from ";;;;"'' as ";";]],
            [[ select 100;]],
        }
        sql = 'select ";" from ";;;;"\'\' as ";"; select 100;     \n\n'
        res = sql_tokenizer.split_sql(sql)
        t.assert_equals(res, exp)

        exp = {
            "create trigger begin\n"..
            "select 1; select 2;\n"..
            "create table a (b,c,d);\n"..
            "end;",
        }
        res = sql_tokenizer.split_sql('create trigger begin\n'..
                                      'select 1; select 2;\n'..
                                      'create table a (b,c,d);\n'..
                                      'end;'
                                      )
        t.assert_equals(res, exp)

        sql = '-- comment1 comment2 comment3; select 1; select 2;'
        t.assert_equals(sql_tokenizer.split_sql(sql), {})

        exp = {
            "select 1;",
            " select /* comment1 comment2 select 100; select 1000; */ 3;",
            " select 2;",
        }
        sql = "select 1; select /* comment1 comment2 select 100; "..
              "select 1000; */ 3; select 2;"
        res = sql_tokenizer.split_sql(sql)
        t.assert_equals(res, exp)

        sql = "select abc from test where case abc when 100 "..
              "then kek else grek end;"
        exp = {
            sql,
        }
        res = sql_tokenizer.split_sql(sql)
        t.assert_equals(res, exp)

        exp = {
            "create temp trigger begin\n"..
            "select case cond when a 100 else 200 end;\n"..
            "insert into tst values v a l u e s;\n"..
            "end;",
            "\n\n\n\t\t\n\n select 200..\";select1000\";",
        }
        res = sql_tokenizer.split_sql('create temp trigger begin\n'..
                                      'select case cond when a 100 '..
                                      'else 200 end;\n'..
                                      'insert into tst values v a l u e s;\n'..
                                      'end;\n\n\n\t\t\n\n select 200..";'..
                                      'select1000";'
                                      )
        t.assert_equals(res, exp)

        exp = {
            "select 1;",
            " select 2;",
        }
        res = sql_tokenizer.split_sql('select 1; -- test comment\n; select 2;')
        t.assert_equals(res, exp)

        exp = {
            "START TRANSACTION;",
            " insert into table values ('example kek--');",
            " commit;",
        }
        sql = "START TRANSACTION; insert into table values "..
              "('example kek--'); commit;"
        res = sql_tokenizer.split_sql(sql)
        t.assert_equals(res, exp)

        exp = {
            "insert into quoted(a) value 'lalala'",
        }
        res = sql_tokenizer.split_sql("insert into quoted(a) value 'lalala'")
        t.assert_equals(res, exp)

        exp = {
            "insert into table values"..
            "('textextextext''textextext''extext--extext/*ext''ext*/');",
            " commit;",
        }
        res = sql_tokenizer.split_sql("insert into table values"..
                                      "('textextextext''textextext''extext"..
                                      "--extext/*ext''ext*/'); "..
                                      "commit;"
                                      )
        t.assert_equals(res, exp)
    end)
end
