#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(21)

-- This suite is aimed to test ALTER TABLE ADD CONSTRAINT statement.
--

test:do_catchsql_test(
    "alter2-1.1",
    [[
        CREATE TABLE t1(id INT PRIMARY KEY, a INT, b INT);
        ALTER TABLE t1 ADD CONSTRAINT fk1 FOREIGN KEY (a) REFERENCES t1(id);
        ALTER TABLE t1 ADD CONSTRAINT fk2 FOREIGN KEY (a) REFERENCES t1;
        INSERT INTO t1 VALUES(1, 1, 2);
    ]], {
        -- <alter2-1.1>
        0
        -- </alter2-1.1>
    })

test:do_catchsql_test(
    "alter2-1.2",
    [[
        INSERT INTO t1 VALUES(2, 3, 2);
    ]], {
        -- <alter2-1.2>
        1, "FOREIGN KEY constraint failed"
        -- </alter2-1.2>
    })

test:do_catchsql_test(
    "alter2-1.3",
    [[
        DELETE FROM t1;
    ]], {
        -- <alter2-1.3>
        0
        -- </alter2-1.3>
    })

test:do_catchsql_test(
    "alter2-1.4",
    [[
        ALTER TABLE t1 DROP CONSTRAINT fk1;
        INSERT INTO t1 VALUES(2, 3, 2);
    ]], {
        -- <alter2-1.4>
        1, "FOREIGN KEY constraint failed"
        -- </alter2-1.4>
    })

test:do_execsql_test(
    "alter2-1.5",
    [[
        ALTER TABLE t1 DROP CONSTRAINT fk2;
        INSERT INTO t1 VALUES(2, 3, 2);
        SELECT * FROM t1;
    ]], {
        -- <alter2-1.5>
        2, 3, 2
        -- </alter2-1.5>
    })

test:do_test(
    "alter2-1.5.1",
    function()
        test:execsql([[DELETE FROM t1;]])
        if box.space.T1.engine == 'vinyl' then
            -- trigger dump to empty the space
            box.snapshot()
        end
    end, {
        -- <alter2-1.5.1>
        -- </alter2-1.5.1>
    })

test:do_catchsql_test(
    "alter2-1.6",
    [[
        CREATE UNIQUE INDEX i1 ON t1(b, a);
        ALTER TABLE t1 ADD CONSTRAINT fk1 FOREIGN KEY (a, b) REFERENCES t1(b, a);
        INSERT INTO t1 VALUES(3, 1, 1);
        INSERT INTO t1 VALUES(4, 2, 1);
    ]], {
        -- <alter2-1.6>
        1, "FOREIGN KEY constraint failed"
        -- </alter2-1.6>
    })

test:do_execsql_test(
    "alter2-1.7",
    [[
        ALTER TABLE t1 DROP CONSTRAINT fk1;
        INSERT INTO t1 VALUES(5, 2, 1);
        SELECT * FROM t1;
    ]], {
        -- <alter2-1.7>
        3, 1, 1, 5, 2, 1
        -- </alter2-1.7>
    })

test:do_test(
    "alter2-1.7.1",
    function()
        test:execsql([[DELETE FROM t1;]])
        if box.space.T1.engine == 'vinyl' then
            -- trigger dump to empty the space
            box.snapshot()
        end
    end, {
        -- <alter2-1.7.1>
        -- </alter2-1.7.1>
    })

test:do_catchsql_test(
    "alter2-1.8",
    [[
        ALTER TABLE t1 ADD CONSTRAINT fk1 FOREIGN KEY (a) REFERENCES t1(id);
        ALTER TABLE t1 ADD CONSTRAINT fk2 FOREIGN KEY (a, b) REFERENCES t1(b, a);
        DROP TABLE t1;
    ]], {
        -- <alter2-1.8>
        0
        -- </alter2-1.8>
    })

test:do_execsql_test(
    "alter2-1.9",
    [[
        SELECT * FROM "_fk_constraint";
    ]], {
        -- <alter2-1.9>
        -- </alter2-1.9>
    })

test:do_catchsql_test(
    "alter2-2.1",
    [[
        CREATE TABLE child (id INT PRIMARY KEY, a INT, b INT);
        CREATE TABLE parent (id INT PRIMARY KEY, c INT UNIQUE, d INT);
        ALTER TABLE child ADD CONSTRAINT fk FOREIGN KEY (id) REFERENCES parent(c);
        ALTER TABLE parent ADD CONSTRAINT fk FOREIGN KEY (c) REFERENCES parent;
        INSERT INTO parent VALUES(1, 2, 3);
    ]], {
        -- <alter2-2.1>
        1, "FOREIGN KEY constraint failed"
        -- </alter2-2.1>
    })

test:do_catchsql_test(
    "alter2-2.2",
    [[
        INSERT INTO parent VALUES(1, 1, 2);
        INSERT INTO child VALUES(2, 1, 1);
    ]], {
        -- <alter2-2.2>
        1, "FOREIGN KEY constraint failed"
        -- </alter2-2.2>
    })

test:do_catchsql_test(
    "alter2-2.3",
    [[
        ALTER TABLE child DROP CONSTRAINT fk;
        INSERT INTO parent VALUES(3, 4, 2);
    ]], {
        -- <alter2-2.3>
        1, "FOREIGN KEY constraint failed"
        -- </alter2-2.3>
    })

test:do_execsql_test(
    "alter2-2.4",
    [[
        ALTER TABLE parent DROP CONSTRAINT fk;
        INSERT INTO parent VALUES(3, 4, 2);
        SELECT * FROM parent;
    ]], {
        -- <alter2-2.4>
        1, 1, 2, 3, 4, 2
        -- </alter2-2.4>
    })

test:do_execsql_test(
    "alter2-3.1",
    [[
        DROP TABLE child;
        DROP TABLE parent;
        CREATE TABLE child (id INT PRIMARY KEY, a INT, b INT);
        CREATE TABLE parent (id INT PRIMARY KEY, c INT, d INT);
        ALTER TABLE child ADD CONSTRAINT fk FOREIGN KEY (id) REFERENCES parent ON DELETE CASCADE MATCH FULL;
        INSERT INTO parent VALUES(1, 2, 3), (3, 4, 5), (6, 7, 8);
        INSERT INTO child VALUES(1, 1, 1), (3, 2, 2);
        DELETE FROM parent WHERE id = 1;
        SELECT * FROM CHILD;
    ]], {
        -- <alter2-3.1>
        3, 2, 2
        -- </alter2-3.1>
    })

test:do_execsql_test(
    "alter2-3.2",
    [[
        DROP TABLE child;
        DROP TABLE parent;
        CREATE TABLE child (id INT UNIQUE, a INT, b INT, z INT PRIMARY KEY AUTOINCREMENT);
        CREATE TABLE parent (id INT UNIQUE, c INT, d INT, z INT PRIMARY KEY AUTOINCREMENT);
        ALTER TABLE child ADD CONSTRAINT fk FOREIGN KEY (id) REFERENCES parent(id) ON UPDATE CASCADE MATCH PARTIAL;
        INSERT INTO parent(id, c, d) VALUES(1, 2, 3), (3, 4, 5), (6, 7, 8);
        INSERT INTO child(id, a, b) VALUES(1, 1, 1), (3, 2, 2);
        UPDATE parent SET id = 5 WHERE id = 1;
        SELECT id,a,b FROM CHILD ORDER BY id,a,b;
    ]], {
        -- <alter2-3.2>
        3, 2, 2, 5, 1, 1
        -- </alter2-3.2>
    })

test:do_catchsql_test(
    "alter2-4.1",
    [[
        ALTER TABLE child ADD CONSTRAINT fk FOREIGN KEY REFERENCES child(id);
    ]], {
        -- <alter2-4.1>
        1, "near \"REFERENCES\": syntax error"
        -- </alter2-4.1>
    })

test:do_catchsql_test(
    "alter2-4.2",
    [[
        ALTER TABLE child ADD CONSTRAINT fk () FOREIGN KEY REFERENCES child(id);
    ]], {
        -- <alter2-4.1>
        1, "near \"(\": syntax error"
        -- </alter2-4.2>
    })

test:do_catchsql_test(
    "alter2-5.1",
    [[
        DROP TABLE child;
        CREATE TABLE child (id INT PRIMARY KEY, a INT UNIQUE);
        ALTER TABLE child ADD CONSTRAINT fk FOREIGN KEY (id) REFERENCES child;
        ALTER TABLE child ADD CONSTRAINT fk FOREIGN KEY (a) REFERENCES child;
    ]], {
        -- <alter2-5.1>
        1, "Constraint FK already exists"
        -- </alter2-5.1>
    })

test:do_catchsql_test(
    "alter2-5.2",
    [[
        ALTER TABLE child DROP CONSTRAINT fake;
    ]], {
        -- <alter2-5.2>
        1, "Constraint FAKE does not exist"
        -- </alter2-5.2>
    })

test:finish_test()
