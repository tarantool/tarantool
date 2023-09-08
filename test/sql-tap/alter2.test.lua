#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(22)

-- This suite is aimed to test ALTER TABLE ADD CONSTRAINT statement.
--

test:do_catchsql_test(
    "alter2-1.1",
    [[
        CREATE TABLE t1(id INT PRIMARY KEY, a INT UNIQUE, b INT);
        ALTER TABLE t1 ADD CONSTRAINT fk1 FOREIGN KEY (a) REFERENCES t1(id);
        ALTER TABLE t1 ADD CONSTRAINT fk2 FOREIGN KEY (a) REFERENCES t1;
        INSERT INTO t1 VALUES(1, NULL, 2);
        INSERT INTO t1 VALUES(0, 1, 2);
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
        1, "Foreign key constraint 'fk2' failed: foreign tuple was not found"
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
        1, "Foreign key constraint 'fk2' failed: foreign tuple was not found"
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
        if box.space.t1.engine == 'vinyl' then
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
        1, "Foreign key constraint 'fk1' failed: foreign tuple was not found"
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
        5, 2, 1
        -- </alter2-1.7>
    })

test:do_test(
    "alter2-1.7.1",
    function()
        test:execsql([[DELETE FROM t1;]])
        if box.space.t1.engine == 'vinyl' then
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
        1, "Foreign key constraint 'fk' failed: foreign tuple was not found"
        -- </alter2-2.1>
    })

test:do_catchsql_test(
    "alter2-2.2",
    [[
        INSERT INTO parent VALUES(1, 1, 2);
        INSERT INTO child VALUES(2, 1, 1);
    ]], {
        -- <alter2-2.2>
        1, "Foreign key constraint 'fk' failed: foreign tuple was not found"
        -- </alter2-2.2>
    })

test:do_catchsql_test(
    "alter2-2.3",
    [[
        ALTER TABLE child DROP CONSTRAINT fk;
        INSERT INTO parent VALUES(3, 4, 2);
    ]], {
        -- <alter2-2.3>
        1, "Foreign key constraint 'fk' failed: foreign tuple was not found"
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
        3, 4, 2
        -- </alter2-2.4>
    })

test:do_catchsql_test(
    "alter2-4.1",
    [[
        ALTER TABLE child ADD CONSTRAINT fk FOREIGN KEY REFERENCES child(id);
    ]], {
        -- <alter2-4.1>
        1, "At line 1 at or near position 57: keyword 'REFERENCES' is reserved. Please use double quotes if 'REFERENCES' is an identifier."
        -- </alter2-4.1>
    })

test:do_catchsql_test(
    "alter2-4.2",
    [[
        ALTER TABLE child ADD CONSTRAINT fk () FOREIGN KEY REFERENCES child(id);
    ]], {
        -- <alter2-4.1>
        1, "Syntax error at line 1 near '('"
        -- </alter2-4.2>
    })

test:do_catchsql_test(
    "alter2-5.2",
    [[
        DROP TABLE child;
        CREATE TABLE child (id INT PRIMARY KEY, a INT UNIQUE);
        ALTER TABLE child DROP CONSTRAINT fake;
    ]], {
        -- <alter2-5.2>
        1, "Constraint 'fake' does not exist in space 'child'"
        -- </alter2-5.2>
    })

-- Test ADD CONSTRAINT CHECK functionality. CHECK constraints are
-- integrated into Tarantool's core, so basically we would test
-- only grammar and validate correctness of raised errors.
--
test:do_catchsql_test(
    "alter2-6.1",
    [[
        CREATE TABLE t1 (id INT PRIMARY KEY);
        ALTER TABLE t1 ADD CONSTRAINT ck CHECK(id > 0);
        INSERT INTO t1 VALUES (-1);
    ]], { 1, "Check constraint 'ck' failed for a tuple" })

-- Make sure that one can't create constraint with the same name twice.
--
test:do_catchsql_test(
    "alter2-6.2",
    [[
        ALTER TABLE t1 ADD CONSTRAINT ck CHECK(id > 0);
    ]], { 1, "Function for the check constraint 'ck' with name 'check_t1_ck' "..
         "already exists"})

-- Make sure that CHECK constraint can be created only on empty space.
--
test:do_catchsql_test(
    "alter2-6.3",
    [[
        INSERT INTO t1 VALUES (1);
        ALTER TABLE t1 ADD CONSTRAINT ck1 CHECK(id > 0);
    ]],
    {1, "Can not perform space format check in a multi-statement transaction"})

-- "Non-existant" space error is raised correctly.
--
test:do_catchsql_test(
    "alter2-6.4",
    [[
        ALTER TABLE fake ADD CONSTRAINT ck CHECK(id > 0);
    ]], { 1, "Space 'fake' does not exist" })

test:finish_test()
