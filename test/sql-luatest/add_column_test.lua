local server = require('luatest.server')
local t = require('luatest')

local g = t.group("column", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s;']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-3075: Check <ALTER TABLE table ADD COLUMN column> statement.
--
g.test_3075_add_column = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE t1 (a INT PRIMARY KEY);")

        -- COLUMN keyword is optional. Check it here, but omit it below.
        local res = box.execute("ALTER TABLE t1 ADD COLUMN b INT;")
        t.assert_equals(res, {row_count = 1})

        -- A column with the same name already exists.
        local _, err = box.execute("ALTER TABLE t1 ADD b SCALAR;")
        t.assert_equals(err.message, "Space field 'b' is duplicate")

        -- Can't add column to a view.
        box.execute("CREATE VIEW v AS SELECT * FROM t1;")
        _, err = box.execute("ALTER TABLE v ADD c INT;")
        local exp_err = "Can't modify space 'v': view can not be altered"
        t.assert_equals(err.message, exp_err)

        local view = box.space._space.index[2]:select('v')[1]:totable()
        local view_format = view[7]
        local f = {type = 'string', nullable_action = 'none',
                   name = 'c', is_nullable = true}
        table.insert(view_format, f)
        view[5] = 3
        view[7] = view_format
        local sp = box.space._space
        t.assert_error_msg_equals(exp_err, sp.replace, sp, view)
        t.assert_equals(err.message, exp_err)

        box.execute("DROP VIEW v;")

        -- Check PRIMARY KEY constraint works with an added column.
        box.execute("CREATE TABLE pk_check (a INT CONSTRAINT pk PRIMARY KEY);")
        res = box.execute("ALTER TABLE pk_check DROP CONSTRAINT pk;")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("ALTER TABLE pk_check ADD b INT PRIMARY KEY;")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO pk_check VALUES (1, 1);")
        t.assert_equals(res, {row_count = 1})
        _, err = box.execute("INSERT INTO pk_check VALUES (1, 1);")
        exp_err = 'Duplicate key exists in unique index ' ..
                  '"pk_unnamed_pk_check_1" in space "pk_check" ' ..
                  'with old tuple - [1, 1] and new tuple - [1, 1]'
        t.assert_equals(err.message, exp_err)
        box.execute("DROP TABLE pk_check;")

        -- Check UNIQUE constraint works with an added column.
        box.execute("CREATE TABLE unique_check (a INT PRIMARY KEY);")
        res = box.execute("ALTER TABLE unique_check ADD b INT UNIQUE;")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO unique_check VALUES (1, 1);")
        t.assert_equals(res, {row_count = 1})
        _, err = box.execute("INSERT INTO unique_check VALUES (2, 1);")
        exp_err = 'Duplicate key exists in unique index ' ..
                  '"unique_unnamed_unique_check_2" in space "unique_check" ' ..
                  'with old tuple - [1, 1] and new tuple - [2, 1]'
        t.assert_equals(err.message, exp_err)
        box.execute("DROP TABLE unique_check;")

        -- Check CHECK constraint works with an added column.
        box.execute("CREATE TABLE ck_check (a INT PRIMARY KEY);")
        res = box.execute("ALTER TABLE ck_check ADD b INT CHECK (b > 0);")
        t.assert_equals(res, {row_count = 1})
        _, err = box.execute("INSERT INTO ck_check VALUES (1, 0);")
        exp_err = "Check constraint 'ck_unnamed_ck_check_b_1' " ..
                  "failed for field '2 (b)'"
        t.assert_equals(err.message, exp_err)
        res = box.execute("INSERT INTO ck_check VALUES (1, 1);")
        t.assert_equals(res, {row_count = 1})
        box.execute("DROP TABLE ck_check;")
        box.execute([[DELETE FROM "_func" WHERE
                      "name" == 'check_ck_check_ck_unnamed_ck_check_b_1';]])

        -- Check FOREIGN KEY constraint works with an added column.
        box.execute("CREATE TABLE fk_check (a INT PRIMARY KEY);")
        res = box.execute("ALTER TABLE fk_check ADD b INT REFERENCES t1(a);")
        t.assert_equals(res, {row_count = 1})
        _, err = box.execute("INSERT INTO fk_check VALUES (0, 1);")
        exp_err = "Foreign key constraint 'fk_unnamed_fk_check_b_1' failed " ..
                  "for field '2 (b)': foreign tuple was not found"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("INSERT INTO fk_check VALUES (2, 0);")
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("INSERT INTO fk_check VALUES (2, 1);")
        t.assert_equals(err.message, exp_err)
        res = box.execute("INSERT INTO t1 VALUES (1, 1);")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO fk_check VALUES (2, 1);")
        t.assert_equals(res, {row_count = 1})
        box.execute("DROP TABLE fk_check;")
        box.execute("DROP TABLE t1;")

        -- Check FOREIGN KEY (self-referenced) constraint works with an
        -- added column.
        box.execute([[CREATE TABLE self (id INT PRIMARY KEY AUTOINCREMENT,
                                         a INT UNIQUE);]])
        res = box.execute("ALTER TABLE self ADD b INT REFERENCES self(a);")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO self(a, b) VALUES(1, NULL);")
        local exp = {
            autoincrement_ids = {1},
            row_count = 1,
        }
        t.assert_equals(res, exp)
        res = box.execute("INSERT INTO self(a, b) VALUES(2, 1);")
        exp = {
            autoincrement_ids = {2},
            row_count = 1,
        }
        t.assert_equals(res, exp)
        res = box.execute("UPDATE self SET b = 2;")
        t.assert_equals(res, {row_count = 2})
        _, err = box.execute("UPDATE self SET b = 3;")
        exp_err = "Foreign key constraint 'fk_unnamed_self_b_1' failed for " ..
                  "field '3 (b)': foreign tuple was not found"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("UPDATE self SET a = 3;")
        exp_err = "Foreign key 'fk_unnamed_self_b_1' integrity check " ..
                  "failed: index was not found"
        t.assert_equals(err.message, exp_err)
        box.execute("DROP TABLE self;")

        -- Check AUTOINCREMENT works with an added column.
        box.execute([[CREATE TABLE autoinc_check
                      (a INT CONSTRAINT pk PRIMARY KEY);]])
        res = box.execute("ALTER TABLE autoinc_check DROP CONSTRAINT pk;")
        t.assert_equals(res, {row_count = 1})
        res = box.execute([[ALTER TABLE autoinc_check
                            ADD b INT PRIMARY KEY AUTOINCREMENT;]])
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO autoinc_check(a) VALUES(1);")
        exp = {
            autoincrement_ids = {1},
            row_count = 1,
        }
        t.assert_equals(res, exp)
        res = box.execute("INSERT INTO autoinc_check(a) VALUES(1);")
        exp = {
            autoincrement_ids = {2},
            row_count = 1,
        }
        t.assert_equals(res, exp)
        box.execute("TRUNCATE TABLE autoinc_check;")

        -- Can't add second column with AUTOINCREMENT.
        local sql = "ALTER TABLE autoinc_check ADD c INT AUTOINCREMENT;"
        _, err = box.execute(sql)
        exp_err = "Can't add AUTOINCREMENT: space autoinc_check can't " ..
                  "feature more than one AUTOINCREMENT field"
        t.assert_equals(err.message, exp_err)
        box.execute("DROP TABLE autoinc_check;")

        -- Check COLLATE clause works with an added column.
        box.execute("CREATE TABLE collate_check (a INT PRIMARY KEY);")
        sql = 'ALTER TABLE collate_check ADD b TEXT COLLATE "unicode_ci";'
        res = box.execute(sql)
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO collate_check VALUES (1, 'a');")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO collate_check VALUES (2, 'A');")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("SELECT * FROM collate_check WHERE b LIKE 'a';")
        t.assert_equals(res.rows, {{1, 'a'}, {2, 'A'}})
        box.execute("DROP TABLE collate_check;")

        -- Check DEFAULT clause works with an added column.
        box.execute("CREATE TABLE default_check (a INT PRIMARY KEY);")
        res = box.execute("ALTER TABLE default_check ADD b TEXT DEFAULT ('a');")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO default_check(a) VALUES (1);")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("SELECT * FROM default_check;")
        t.assert_equals(res.rows, {{1, 'a'}})
        box.execute("DROP TABLE default_check;")

        -- Check NULL constraint works with an added column.
        box.execute("CREATE TABLE null_check (a INT PRIMARY KEY);")
        res = box.execute("ALTER TABLE null_check ADD b TEXT NULL;")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO null_check(a) VALUES (1);")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("SELECT * FROM null_check;")
        t.assert_equals(res.rows, {{1, nil}})
        box.execute("DROP TABLE null_check;")

        -- Check NOT NULL constraint works with an added column.
        box.execute("CREATE TABLE notnull_check (a INT PRIMARY KEY);")
        res = box.execute("ALTER TABLE notnull_check ADD b TEXT NOT NULL;")
        t.assert_equals(res, {row_count = 1})
        _, err = box.execute("INSERT INTO notnull_check(a) VALUES (1);")
        exp_err = "Failed to execute SQL statement: NOT NULL " ..
                  "constraint failed: notnull_check.b"
        t.assert_equals(err.message, exp_err)
        res = box.execute("INSERT INTO notnull_check VALUES (1, 'not null');")
        t.assert_equals(res, {row_count = 1})
        box.execute("DROP TABLE notnull_check;")

        -- Can't add a column with DEAFULT or NULL to a non-empty space.
        -- This ability isn't implemented yet.
        box.execute("CREATE TABLE non_empty (a INT PRIMARY KEY);")
        res = box.execute("INSERT INTO non_empty VALUES (1);")
        t.assert_equals(res, {row_count = 1})
        _, err = box.execute("ALTER TABLE non_empty ADD b INT NULL;")
        exp_err = "Tuple field count 1 does not match space field count 2"
        t.assert_equals(err.message, exp_err)
        _, err = box.execute("ALTER TABLE non_empty ADD b INT DEFAULT (1);")
        t.assert_equals(err.message, exp_err)
        box.execute("DROP TABLE non_empty;")

        -- Add to a no-SQL adjusted space without format.
        _ = box.schema.space.create('without_format')
        box.execute("ALTER TABLE without_format ADD a INT PRIMARY KEY;")
        box.execute("INSERT INTO without_format VALUES (1);")
        box.execute("DROP TABLE without_format;")

        -- Add to a no-SQL adjusted space with format.
        local with_format = box.schema.space.create('with_format')
        with_format:format{{name = 'A', type = 'unsigned'}}
        res = box.execute("ALTER TABLE with_format ADD b INT PRIMARY KEY;")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO with_format VALUES (1, 1);")
        t.assert_equals(res, {row_count = 1})
        box.execute("DROP TABLE with_format;")

        -- Add multiple columns (with a constraint) inside a transaction.
        box.execute("CREATE TABLE t2 (a INT PRIMARY KEY);")
        box.begin()
        res = box.execute('ALTER TABLE t2 ADD b INT;')
        t.assert_equals(res, {row_count = 1})
        res = box.execute('ALTER TABLE t2 ADD c INT UNIQUE;')
        t.assert_equals(res, {row_count = 1})
        box.commit()
        res = box.execute("INSERT INTO t2 VALUES (1, 1, 1);")
        t.assert_equals(res, {row_count = 1})
        _, err = box.execute("INSERT INTO t2 VALUES (2, 1, 1);")
        exp_err = 'Duplicate key exists in unique index ' ..
                  '"unique_unnamed_t2_2" in space "t2" with old tuple - ' ..
                  '[1, 1, 1] and new tuple - [2, 1, 1]'
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT * FROM t2;")
        t.assert_equals(res.rows, {{1, 1, 1}})
        box.execute("DROP TABLE t2;")
    end)
end
