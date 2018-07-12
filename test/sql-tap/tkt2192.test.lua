#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(6)

--!./tcltestrunner.lua
-- 2007 January 26
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library.
--
-- This file implements tests to verify that ticket #2192 has been
-- fixed.  
--
--
-- $Id: tkt2192.test,v 1.3 2008/08/04 03:51:24 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_execsql_test(
    "tkt2192-1.1",
    [[
        -- Raw data (RBS) --------

        create table records (
          date_t        real primary key,
          type          text,
          description   text,
          value         integer,
          acc_name      text,
          acc_no        text
        );

        -- Direct Debits ----------------
        create view direct_debits as
          select * from records where type = 'D/D';

        create view monthly_direct_debits as
          select strftime('%Y-%m', date_t) as "date", (-1 * sum(value)) as value
            from direct_debits
           group by strftime('%Y-%m', date_t);

        -- Expense Categories ---------------
        create view energy as
          select strftime('%Y-%m', date_t) as "date", (-1 * sum(value)) as value
            from direct_debits
           where description like '%NPOWER%'
           group by strftime('%Y-%m', date_t);

        create view phone_internet as
          select strftime('%Y-%m', date_t) as "date", (-1 * sum(value)) as value
            from direct_debits
           where description like '%BT DIRECT%'
              or description like '%SUPANET%'
              or description like '%ORANGE%'
           group by strftime('%Y-%m', date_t);

        create view credit_cards as
          select strftime('%Y-%m', date_t) as "date", (-1 * sum(value)) as value
            from direct_debits where description like '%VISA%'
           group by strftime('%Y-%m', date_t);

        -- Overview ---------------------

        create view expense_overview as
          select 'Energy' as expense, "date", value from energy
          union
          select 'Phone/Internet' as expense, "date", value from phone_internet
          union
          select 'Credit Card' as expense, "date", value from credit_cards;

        create view jan as
          select 'jan', expense, value from expense_overview
           where "date" like '%-01';

        create view nov as
          select 'nov', expense, value from expense_overview
           where "date" like '%-11';

        create view summary as
          select * from jan join nov on (jan.expense = nov.expense);
    ]], {
        -- <tkt2192-1.1>
        
        -- </tkt2192-1.1>
    })


test:do_test(
    "tkt2192-1.2",
    function()
        -- set ::sqlite_addop_trace 1
        return test:execsql [[
            select * from summary;
        ]]
    end, {
        -- <tkt2192-1.2>

        -- </tkt2192-1.2>
    })

test:do_execsql_test(
    "tkt2192-2.1",
    [[
        CREATE TABLE t1(a INT ,b  INT primary key);
        CREATE VIEW v1 AS
          SELECT * FROM t1 WHERE b%7=0 UNION SELECT * FROM t1 WHERE b%5=0;
        INSERT INTO t1 VALUES(1,7);
        INSERT INTO t1 VALUES(2,10);
        INSERT INTO t1 VALUES(3,14);
        INSERT INTO t1 VALUES(4,15);
        INSERT INTO t1 VALUES(1,16);
        INSERT INTO t1 VALUES(2,17);
        INSERT INTO t1 VALUES(3,20);
        INSERT INTO t1 VALUES(4,21);
        INSERT INTO t1 VALUES(1,22);
        INSERT INTO t1 VALUES(2,24);
        INSERT INTO t1 VALUES(3,25);
        INSERT INTO t1 VALUES(4,26);
        INSERT INTO t1 VALUES(1,27);

        SELECT b FROM v1 ORDER BY b;
    ]], {
        -- <tkt2192-2.1>
        7, 10, 14, 15, 20, 21, 25
        -- </tkt2192-2.1>
    })

test:do_execsql_test(
    "tkt2192-2.2",
    [[
        SELECT * FROM v1 ORDER BY a, b;
    ]], {
        -- <tkt2192-2.2>
        1, 7, 2, 10, 3, 14, 3, 20, 3, 25, 4, 15, 4, 21
        -- </tkt2192-2.2>
    })

test:do_execsql_test(
    "tkt2192-2.3",
    [[
        SELECT x.a || '/' || x.b || '/' || y.b
          FROM v1 AS x JOIN v1 AS y ON x.a=y.a AND x.b<y.b
         ORDER BY x.a, x.b, y.b
    ]], {
        -- <tkt2192-2.3>
        "3/14/20", "3/14/25", "3/20/25", "4/15/21"
        -- </tkt2192-2.3>
    })

test:do_execsql_test(
    "tkt2192-2.4",
    [[
        CREATE VIEW v2 AS
        SELECT x.a || '/' || x.b || '/' || y.b AS z
          FROM v1 AS x JOIN v1 AS y ON x.a=y.a AND x.b<y.b
         ORDER BY x.a, x.b, y.b;
        SELECT * FROM v2;
    ]], {
        -- <tkt2192-2.4>
        "3/14/20", "3/14/25", "3/20/25", "4/15/21"
        -- </tkt2192-2.4>
    })

test:finish_test()

