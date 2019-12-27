test_run = require('test_run').new()
json = require('json')
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

--
-- gh-4086: SQL transactional DDL.
--
test_run:cmd("setopt delimiter ';'")
box.begin()
box.execute('CREATE TABLE t1(id INTEGER PRIMARY KEY);')
box.execute('CREATE TABLE t2(id INTEGER PRIMARY KEY);')
box.commit();
test_run:cmd("setopt delimiter ''");

box.space.T1 ~= nil
box.space.T1.index[0] ~= nil
box.space.T2 ~= nil
box.space.T2.index[0] ~= nil

test_run:cmd("setopt delimiter ';'")
box.begin()
box.execute('DROP TABLE t1;')
assert(box.space.T1 == nil)
assert(box.space.T2 ~= nil)
box.execute('DROP TABLE t2;')
assert(box.space.T2 == nil)
box.commit();
test_run:cmd("setopt delimiter ''");

--
-- Try to build an index transactionally.
--
box.execute('CREATE TABLE t1(id INTEGER PRIMARY KEY, a INTEGER, b INTEGER)')
box.space.T1:replace{1, 1, 1}
box.space.T1:replace{2, 2, 2}
box.space.T1:replace{3, 3, 3}
box.space.T1:replace{4, 4, 4}
box.space.T1:replace{5, 5, 5}
-- Snapshot to dump Vinyl memory level, and force reading from a
-- disk, if someday create index will support transactions.
box.snapshot()

test_run:cmd("setopt delimiter ';'")
box.begin(),
box.execute('CREATE TABLE t2(id INTEGER PRIMARY KEY)') or box.error(),
box.execute('CREATE INDEX t1a ON t1(a)') or box.error(),
box.execute('CREATE INDEX t1b ON t1(b)') or box.error(),
box.commit();
test_run:cmd("setopt delimiter ''");
box.rollback()

--
-- Index drop does not yield, and is being done in background
-- later. So it is transactional.
--
box.execute('CREATE INDEX t1a ON t1(a)')
box.execute('CREATE INDEX t1b ON t1(b)')

test_run:cmd("setopt delimiter ';'")
box.begin()
box.execute('CREATE TABLE t2(id INTEGER PRIMARY KEY)')
box.execute('DROP INDEX t1a ON t1')
box.execute('DROP INDEX t1b ON t1')
box.commit()
test_run:cmd("setopt delimiter ''");

--
-- Truncate should not be different from index drop in terms of
-- yields and atomicity.
--
box.space.T2:replace{1}
test_run:cmd("setopt delimiter ';'")
function truncate_both()
    box.execute('TRUNCATE TABLE t1;')
    box.execute('TRUNCATE TABLE t2;')
end;
test_run:cmd("setopt delimiter ''");

box.begin() truncate_both() box.rollback()

box.space.T1:count() > 0 and box.space.T2:count() > 0

box.begin() truncate_both() box.commit()

box.space.T1:count() == 0 and box.space.T2:count() == 0

--
-- Rename transactionally changes name of the table and its
-- mentioning in trigger bodies.
--
_trigger_index = box.space._trigger.index.space_id
test_run:cmd("setopt delimiter '$'")
box.execute([[CREATE TRIGGER t1t AFTER INSERT ON t1 FOR EACH ROW
              BEGIN
                  INSERT INTO t2 VALUES(1);
              END; ]])$

box.begin()
box.execute('ALTER TABLE t1 RENAME TO t1_new;')
sql = _trigger_index:select(box.space.T1_NEW.id)[1].opts.sql
assert(sql:find('T1_NEW'))
box.rollback()$
test_run:cmd("setopt delimiter ''")$

sql = _trigger_index:select(box.space.T1.id)[1].opts.sql
not sql:find('T1_NEW') and sql:find('t1') ~= nil

box.execute('ALTER TABLE t1 RENAME TO t1_new;')
sql = _trigger_index:select(box.space.T1_NEW.id)[1].opts.sql
sql:find('T1_NEW') ~= nil

box.execute('DROP TABLE t1_new')
box.execute('DROP TABLE t2')

--
-- Use all the possible SQL DDL statements.
--
test_run:cmd("setopt delimiter '$'")
function monster_ddl()
-- Try random errors inside this big batch of DDL to ensure, that
-- they do not affect normal operation.
    local _, err1, err2, err3, err4, err5, err6
    box.execute([[CREATE TABLE t1(id INTEGER PRIMARY KEY,
                                  a INTEGER,
                                  b INTEGER);]])
    box.execute([[CREATE TABLE t2(id INTEGER PRIMARY KEY,
                                  a INTEGER,
                                  b INTEGER UNIQUE,
                                  CONSTRAINT ck1 CHECK(b < 100));]])

    box.execute('CREATE INDEX t1a ON t1(a);')
    box.execute('CREATE INDEX t2a ON t2(a);')

    box.execute('CREATE TABLE t_to_rename(id INTEGER PRIMARY KEY, a INTEGER);')

    box.execute('DROP INDEX t2a ON t2;')

    box.execute('CREATE INDEX t_to_rename_a ON t_to_rename(a);')

    box.execute('ALTER TABLE t1 ADD CONSTRAINT ck1 CHECK(b > 0);')

    _, err1 = box.execute('ALTER TABLE t_to_rename RENAME TO t1;')

    box.execute('ALTER TABLE t1 ADD CONSTRAINT ck2 CHECK(a > 0);')
    box.space.T1.ck_constraint.CK1:drop()

    box.execute([[ALTER TABLE t1 ADD CONSTRAINT fk1 FOREIGN KEY
                  (a) REFERENCES t2(b);]])
    box.execute('ALTER TABLE t1 DROP CONSTRAINT fk1;')

    _, err2 = box.execute('CREATE TABLE t1(id INTEGER PRIMARY KEY);')

    box.execute([[ALTER TABLE t1 ADD CONSTRAINT fk1 FOREIGN KEY
                  (a) REFERENCES t2(b);]])

    box.execute([[CREATE TABLE trigger_catcher(id INTEGER PRIMARY
                                               KEY AUTOINCREMENT);]])

    box.execute('ALTER TABLE t_to_rename RENAME TO t_renamed;')

    box.execute('DROP INDEX t_to_rename_a ON t_renamed;')

    box.execute([[CREATE TRIGGER t1t AFTER INSERT ON t1 FOR EACH ROW
                  BEGIN
                      INSERT INTO trigger_catcher VALUES(1);
                  END; ]])

    _, err3 = box.execute('DROP TABLE t3;')

    box.execute([[CREATE TRIGGER t2t AFTER INSERT ON t2 FOR EACH ROW
                  BEGIN
                      INSERT INTO trigger_catcher VALUES(1);
                  END; ]])

    _, err4 = box.execute('CREATE INDEX t1a ON t1(a, b);')

    box.execute('TRUNCATE TABLE t1;')
    _, err5 = box.execute('TRUNCATE TABLE t2;')
    _, err6 = box.execute('TRUNCATE TABLE t_does_not_exist;')

    box.execute('DROP TRIGGER t2t;')

    return {'Finished ok, errors in the middle: ', err1, err2, err3, err4,
            err5, err6}
end$
function monster_ddl_cmp_res(res1, res2)
    if json.encode(res1) == json.encode(res2) then
        return true
    end
    return res1, res2
end$
function monster_ddl_is_clean()
    assert(box.space.T1 == nil)
    assert(box.space.T2 == nil)
    assert(box.space._trigger:count() == 0)
    assert(box.space._fk_constraint:count() == 0)
    assert(box.space._ck_constraint:count() == 0)
    assert(box.space.T_RENAMED == nil)
    assert(box.space.T_TO_RENAME == nil)
end$
function monster_ddl_check()
    local _, err1, err2, err3, err4, res
    _, err1 = box.execute('INSERT INTO t2 VALUES (1, 1, 101)')
    box.execute('INSERT INTO t2 VALUES (1, 1, 1)')
    _, err2 = box.execute('INSERT INTO t2 VALUES(2, 2, 1)')
    _, err3 = box.execute('INSERT INTO t1 VALUES(1, 20, 1)')
    _, err4 = box.execute('INSERT INTO t1 VALUES(1, -1, 1)')
    box.execute('INSERT INTO t1 VALUES (1, 1, 1)')
    res = box.execute('SELECT * FROM trigger_catcher')
    assert(box.space.T_RENAMED ~= nil)
    assert(box.space.T_RENAMED.index.T_TO_RENAME_A == nil)
    assert(box.space.T_TO_RENAME == nil)
    return {'Finished ok, errors and trigger catcher content: ', err1, err2,
            err3, err4, res}
end$
function monster_ddl_clear()
    box.execute('DROP TRIGGER IF EXISTS t1t;')
    box.execute('DROP TABLE IF EXISTS trigger_catcher;')
    box.execute('ALTER TABLE t1 DROP CONSTRAINT fk1;')
    box.execute('DROP TABLE IF EXISTS t2')
    box.execute('DROP TABLE IF EXISTS t1')
    box.execute('DROP TABLE IF EXISTS t_renamed')
    monster_ddl_is_clean()
end$
test_run:cmd("setopt delimiter ''")$

-- No txn.
true_ddl_res = monster_ddl()
true_ddl_res

true_check_res = monster_ddl_check()
true_check_res

monster_ddl_clear()

-- Both DDL and cleanup in one txn.
ddl_res = nil
box.begin() ddl_res = monster_ddl() monster_ddl_clear() box.commit()
monster_ddl_cmp_res(ddl_res, true_ddl_res)

-- DDL in txn, cleanup is not.
box.begin() ddl_res = monster_ddl() box.commit()
monster_ddl_cmp_res(ddl_res, true_ddl_res)

check_res = monster_ddl_check()
monster_ddl_cmp_res(check_res, true_check_res)

monster_ddl_clear()

-- DDL is not in txn, cleanup is.
ddl_res = monster_ddl()
monster_ddl_cmp_res(ddl_res, true_ddl_res)

check_res = monster_ddl_check()
monster_ddl_cmp_res(check_res, true_check_res)

box.begin() monster_ddl_clear() box.commit()

-- DDL and cleanup in separate txns.
box.begin() ddl_res = monster_ddl() box.commit()
monster_ddl_cmp_res(ddl_res, true_ddl_res)

check_res = monster_ddl_check()
monster_ddl_cmp_res(check_res, true_check_res)

box.begin() monster_ddl_clear() box.commit()

-- Try SQL transactions.
box.execute('START TRANSACTION') ddl_res = monster_ddl() box.execute('COMMIT')
monster_ddl_cmp_res(ddl_res, true_ddl_res)

check_res = monster_ddl_check()
monster_ddl_cmp_res(check_res, true_check_res)

box.execute('START TRANSACTION') monster_ddl_clear() box.execute('COMMIT')

box.execute('START TRANSACTION') ddl_res = monster_ddl() box.execute('ROLLBACK')
monster_ddl_cmp_res(ddl_res, true_ddl_res)

--
-- Voluntary rollback.
--
test_run:cmd("setopt delimiter ';'")
box.begin()
box.execute('CREATE TABLE t1(id INTEGER PRIMARY KEY);')
assert(box.space.T1 ~= nil)
box.execute('CREATE TABLE t2(id INTEGER PRIMARY KEY);')
assert(box.space.T2 ~= nil)
box.rollback();

box.space.T1 == nil and box.space.T2 == nil;

box.begin()
save1 = box.savepoint()
box.execute('CREATE TABLE t1(id INTEGER PRIMARY KEY)')
save2 = box.savepoint()
box.execute('CREATE TABLE t2(id INTEGER PRIMARY KEY, a INTEGER)')
box.execute('CREATE INDEX t2a ON t2(a)')
save3 = box.savepoint()
assert(box.space.T1 ~= nil)
assert(box.space.T2 ~= nil)
assert(box.space.T2.index.T2A ~= nil)
box.execute('DROP TABLE t2')
assert(box.space.T2 == nil)
box.rollback_to_savepoint(save3)
assert(box.space.T2 ~= nil)
assert(box.space.T2.index.T2A ~= nil)
save3 = box.savepoint()
box.execute('DROP TABLE t2')
assert(box.space.T2 == nil)
box.rollback_to_savepoint(save2)
assert(box.space.T2 == nil)
assert(box.space.T1 ~= nil)
box.rollback_to_savepoint(save1)
box.commit();
test_run:cmd("setopt delimiter ''");

box.space.T1 == nil and box.space.T2 == nil

--
-- Unexpected rollback.
--

box.begin() ddl_res = monster_ddl() require('fiber').yield()
monster_ddl_cmp_res(ddl_res, true_ddl_res)
box.commit()
box.rollback()
monster_ddl_clear()
