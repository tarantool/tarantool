--
-- gh-3075: Check <ALTER TABLE table ADD COLUMN column> statement.
--
CREATE TABLE t1 (a INT PRIMARY KEY);

--
-- COLUMN keyword is optional. Check it here, but omit it below.
--
ALTER TABLE t1 ADD COLUMN b INT;

--
-- A column with the same name already exists.
--
ALTER TABLE t1 ADD b SCALAR;

--
-- Can't add column to a view.
--
CREATE VIEW v AS SELECT * FROM t1;
ALTER TABLE v ADD c INT;

\set language lua
view = box.space._space.index[2]:select('V')[1]:totable()
view_format = view[7]
f = {type = 'string', nullable_action = 'none', name = 'C', is_nullable = true}
table.insert(view_format, f)
view[5] = 3
view[7] = view_format
box.space._space:replace(view)
\set language sql

DROP VIEW v;

--
-- Check PRIMARY KEY constraint works with an added column.
--
CREATE TABLE pk_check (a INT CONSTRAINT pk PRIMARY KEY);
ALTER TABLE pk_check DROP CONSTRAINT pk;
ALTER TABLE pk_check ADD b INT PRIMARY KEY;
INSERT INTO pk_check VALUES (1, 1);
INSERT INTO pk_check VALUES (1, 1);
DROP TABLE pk_check;

--
-- Check UNIQUE constraint works with an added column.
--
CREATE TABLE unique_check (a INT PRIMARY KEY);
ALTER TABLE unique_check ADD b INT UNIQUE;
INSERT INTO unique_check VALUES (1, 1);
INSERT INTO unique_check VALUES (2, 1);
DROP TABLE unique_check;

--
-- Check CHECK constraint works with an added column.
--
CREATE TABLE ck_check (a INT PRIMARY KEY);
ALTER TABLE ck_check ADD b INT CHECK (b > 0);
INSERT INTO ck_check VALUES (1, 0);
INSERT INTO ck_check VALUES (1, 1);
DROP TABLE ck_check;

--
-- Check FOREIGN KEY constraint works with an added column.
--
CREATE TABLE fk_check (a INT PRIMARY KEY);
ALTER TABLE fk_check ADD b INT REFERENCES t1(a);
INSERT INTO fk_check VALUES (0, 1);
INSERT INTO fk_check VALUES (2, 0);
INSERT INTO fk_check VALUES (2, 1);
INSERT INTO t1 VALUES (1, 1);
INSERT INTO fk_check VALUES (2, 1);
DROP TABLE fk_check;
DROP TABLE t1;
--
-- Check FOREIGN KEY (self-referenced) constraint works with an
-- added column.
--
CREATE TABLE self (id INT PRIMARY KEY AUTOINCREMENT, a INT UNIQUE)
ALTER TABLE self ADD b INT REFERENCES self(a)
INSERT INTO self(a,b) VALUES(1, 1);
UPDATE self SET a = 2, b = 2;
UPDATE self SET b = 3;
UPDATE self SET a = 3;
DROP TABLE self;

--
-- Check AUTOINCREMENT works with an added column.
--
CREATE TABLE autoinc_check (a INT CONSTRAINT pk PRIMARY KEY);
ALTER TABLE autoinc_check DROP CONSTRAINT pk;
ALTER TABLE autoinc_check ADD b INT PRIMARY KEY AUTOINCREMENT;
INSERT INTO autoinc_check(a) VALUES(1);
INSERT INTO autoinc_check(a) VALUES(1);
TRUNCATE TABLE autoinc_check;

--
-- Can't add second column with AUTOINCREMENT.
--
ALTER TABLE autoinc_check ADD c INT AUTOINCREMENT;
DROP TABLE autoinc_check;

--
-- Check COLLATE clause works with an added column.
--
CREATE TABLE collate_check (a INT PRIMARY KEY);
ALTER TABLE collate_check ADD b TEXT COLLATE "unicode_ci";
INSERT INTO collate_check VALUES (1, 'a');
INSERT INTO collate_check VALUES (2, 'A');
SELECT * FROM collate_check WHERE b LIKE 'a';
DROP TABLE collate_check;

--
-- Check DEFAULT clause works with an added column.
--
CREATE TABLE default_check (a INT PRIMARY KEY);
ALTER TABLE default_check ADD b TEXT DEFAULT ('a');
INSERT INTO default_check(a) VALUES (1);
SELECT * FROM default_check;
DROP TABLE default_check;

--
-- Check NULL constraint works with an added column.
--
CREATE TABLE null_check (a INT PRIMARY KEY);
ALTER TABLE null_check ADD b TEXT NULL;
INSERT INTO null_check(a) VALUES (1);
SELECT * FROM null_check;
DROP TABLE null_check;

--
-- Check NOT NULL constraint works with an added column.
--
CREATE TABLE notnull_check (a INT PRIMARY KEY);
ALTER TABLE notnull_check ADD b TEXT NOT NULL;
INSERT INTO notnull_check(a) VALUES (1);
INSERT INTO notnull_check VALUES (1, 'not null');
DROP TABLE notnull_check;

--
-- Can't add a column with DEAFULT or NULL to a non-empty space.
-- This ability isn't implemented yet.
--
CREATE TABLE non_empty (a INT PRIMARY KEY);
INSERT INTO non_empty VALUES (1);
ALTER TABLE non_empty ADD b INT NULL;
ALTER TABLE non_empty ADD b INT DEFAULT (1);
DROP TABLE non_empty;

--
-- Add to a no-SQL adjusted space without format.
--
\set language lua
_ = box.schema.space.create('WITHOUT_FORMAT')
\set language sql
ALTER TABLE without_format ADD a INT PRIMARY KEY;
INSERT INTO without_format VALUES (1);
DROP TABLE without_format;

--
-- Add to a no-SQL adjusted space with format.
--
\set language lua
with_format = box.schema.space.create('WITH_FORMAT')
with_format:format{{name = 'A', type = 'unsigned'}}
\set language sql
ALTER TABLE with_format ADD b INT PRIMARY KEY;
INSERT INTO with_format VALUES (1, 1);
DROP TABLE with_format;

--
-- Add multiple columns (with a constraint) inside a transaction.
--
CREATE TABLE t2 (a INT PRIMARY KEY)
\set language lua
box.begin()                                                                     \
box.execute('ALTER TABLE t2 ADD b INT')                                         \
box.execute('ALTER TABLE t2 ADD c INT UNIQUE')                                  \
box.commit()
\set language sql
INSERT INTO t2 VALUES (1, 1, 1);
INSERT INTO t2 VALUES (2, 1, 1);
SELECT * FROM t2;
DROP TABLE t2;
