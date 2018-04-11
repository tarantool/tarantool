#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(2)

test:do_test(
    "format-leak-prep",
    function()
        box.sql.execute("CREATE TABLE t1(id UNSIGNED BIG INT PRIMARY KEY,\
                         max_players INTEGER, n_players INTEGER, flags INTEGER);");
        box.sql.execute("CREATE INDEX IDX_MAX_PLAYERS ON t1(max_players);");
        box.sql.execute("CREATE INDEX IDX_N_PLAYERS ON t1(n_players);");
        box.sql.execute("CREATE INDEX IDX_FLAGS ON t1(flags);");
        for i = 1, 10 do
            box.sql.execute(string.format("INSERT INTO t1 VALUES (%s, %s, %s, %s);",
                                          i, 15, 6, 3));
        end
    end, {

    })

test:do_test(
    "format-leak",
    function()
        for i = 1, 100000 do
            box.sql.execute("SELECT id FROM t1 WHERE flags=3 ORDER BY id LIMIT 2");
        end
    end, {

    })

test:finish_test()
