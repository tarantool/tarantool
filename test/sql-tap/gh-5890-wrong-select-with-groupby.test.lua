#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(2)

--
-- Make sure the SELECT result does not change if GROUP BY is used in case one of
-- selected values is also used in GROUP BY and is a VARBINARY that is not
-- directly received from space.
--
test:do_execsql_test(
    "gh-5890-1",
    [[
        CREATE TABLE t(i INT PRIMARY KEY, v VARBINARY);
        INSERT INTO t VALUES(1, x'6178'), (2, x'6278'), (3, x'6379');
        SELECT count(*), substr(v,2,1) AS m FROM t GROUP BY m;
    ]], {
        2, 'x', 1, 'y'
    })

test:do_execsql_test(
    "gh-5890-2",
    [[
        SELECT count(*), v || v AS m FROM t GROUP BY m;
    ]], {
        1, 'axax', 1, 'bxbx', 1, 'cycy'
    })

test:finish_test()
