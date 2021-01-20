#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(2)

test:do_test(
    "format-leak-prep",
    function()
        box.execute("CREATE TABLE t1(id INTEGER PRIMARY KEY,\
                         max_players INTEGER, n_players INTEGER, flags INTEGER);");
        box.execute("CREATE INDEX IDX_MAX_PLAYERS ON t1(max_players);");
        box.execute("CREATE INDEX IDX_N_PLAYERS ON t1(n_players);");
        box.execute("CREATE INDEX IDX_FLAGS ON t1(flags);");
        for i = 1, 10 do
            box.execute(string.format("INSERT INTO t1 VALUES (%s, %s, %s, %s);",
                                          i, 15, 6, 3));
        end
    end, {

    })

test:do_test(
    "format-leak",
    function()
        for _ = 1, 100000 do
            box.execute("SELECT id FROM t1 WHERE flags=3 ORDER BY id LIMIT 2");
        end
    end, {

    })

test:finish_test()
