local server = require('luatest.server')
local t = require('luatest')
local g = t.group("view", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-4086: SQL transactional DDL.
--
g.test_4086_ddl = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        -- This function makes it easier to find and check errors.
        local function execute(sql)
            local res, err = box.execute(sql)
            if err ~= nil then
                box.error(err)
            end
            return res
        end
        box.begin()
        execute('CREATE TABLE t1(id INTEGER PRIMARY KEY);')
        execute('CREATE TABLE t2(id INTEGER PRIMARY KEY);')
        box.commit()

        t.assert(box.space.t1 ~= nil)
        t.assert(box.space.t1.index[0] ~= nil)
        t.assert(box.space.t2 ~= nil)
        t.assert(box.space.t2.index[0] ~= nil)

        box.begin()
        execute('DROP TABLE t1;')
        t.assert_equals(box.space.t1, nil)
        t.assert(box.space.t2 ~= nil)
        execute('DROP TABLE t2;')
        t.assert_equals(box.space.t2, nil)
        box.commit()

        -- Try to build an index transactionally.
        execute([[CREATE TABLE t1(id INTEGER PRIMARY KEY, a INTEGER,
                                  b INTEGER);]])
        local res = box.space.t1:replace{1, 1, 1}
        t.assert_equals(res, {1, 1, 1})
        res = box.space.t1:replace{2, 2, 2}
        t.assert_equals(res, {2, 2, 2})
        res = box.space.t1:replace{3, 3, 3}
        t.assert_equals(res, {3, 3, 3})
        res = box.space.t1:replace{4, 4, 4}
        t.assert_equals(res, {4, 4, 4})
        res = box.space.t1:replace{5, 5, 5}
        t.assert_equals(res, {5, 5, 5})
        -- Snapshot to dump Vinyl memory level, and force reading from a
        -- disk, if someday create index will support transactions.
        box.snapshot()

        local function transaction()
            box.begin()
            execute('CREATE TABLE t2(id INTEGER PRIMARY KEY);')
            execute('CREATE INDEX t1a ON t1(a);')
            execute('CREATE INDEX t1b ON t1(b);')
            box.commit()
        end
        local exp_err = "Can not perform index build " ..
                        "in a multi-statement transaction"
        t.assert_error_msg_equals(exp_err, transaction)
        box.rollback()

        -- Index drop does not yield, and is being done in background
        -- later. So it is transactional.
        execute('CREATE INDEX t1a ON t1(a);')
        execute('CREATE INDEX t1b ON t1(b);')

        box.begin()
        execute('CREATE TABLE t2(id INTEGER PRIMARY KEY);')
        execute('DROP INDEX t1a ON t1;')
        execute('DROP INDEX t1b ON t1;')
        box.commit()

        -- Truncate should not be different from index drop in terms of
        -- yields and atomicity.
        res = box.space.t2:replace{1}
        t.assert_equals(res, {1})
        local function truncate_both()
            execute('TRUNCATE TABLE t1;')
            execute('TRUNCATE TABLE t2;')
        end;

        box.begin()
        truncate_both()
        box.rollback()

        t.assert(box.space.t1:count() > 0 and box.space.t2:count() > 0)

        box.begin()
        truncate_both()
        box.commit()

        t.assert(box.space.t1:count() == 0 and box.space.t2:count() == 0)

        -- Rename transactionally changes name of the table and its
        -- mentioning in trigger bodies.
        local _trigger_index = box.space._trigger.index.space_id

        res = execute([[CREATE TRIGGER t1t AFTER INSERT ON t1 FOR EACH ROW
                        BEGIN INSERT INTO t2 VALUES(1); END;]])
        t.assert_equals(res, {row_count = 1})

        box.begin()
        execute('ALTER TABLE t1 RENAME TO t1_new;')
        local sql = _trigger_index:select(box.space.t1_new.id)[1].opts.sql
        t.assert(sql:find('t1_new'))
        box.rollback()

        sql = _trigger_index:select(box.space.t1.id)[1].opts.sql
        t.assert(not sql:find('t1_new') and sql:find('t1') ~= nil)

        res = execute('ALTER TABLE t1 RENAME TO t1_new;')
        t.assert_equals(res, {row_count = 0})
        sql = _trigger_index:select(box.space.t1_new.id)[1].opts.sql
        t.assert(sql:find('t1_new') ~= nil)

        res = execute('DROP TABLE t1_new;')
        t.assert_equals(res, {row_count = 1})
        res = execute('DROP TABLE t2;')
        t.assert_equals(res, {row_count = 1})

        -- Use all the possible SQL DDL statements.
        local function monster_ddl()
            -- Try random errors inside this big batch of DDL to ensure, that
            -- they do not affect normal operation.
            local _, err1, err2, err3, err4, err5
            execute([[CREATE TABLE t1(id INTEGER PRIMARY KEY,
                                      a INTEGER,
                                      b INTEGER);]])
            execute([[CREATE TABLE t2(id INTEGER PRIMARY KEY,
                                      a INTEGER,
                                      b INTEGER UNIQUE);]])

            execute('CREATE INDEX t1a ON t1(a);')
            execute('CREATE INDEX t2a ON t2(a);')

            execute([[CREATE TABLE t_to_rename(id INTEGER PRIMARY KEY,
                                               a INTEGER);]])

            execute('DROP INDEX t2a ON t2;')

            execute('CREATE INDEX t_to_rename_a ON t_to_rename(a);')

            _, err1 = box.execute('ALTER TABLE t_to_rename RENAME TO t1;')

            _, err2 = box.execute('CREATE TABLE t1(id INTEGER PRIMARY KEY);')

            execute([[CREATE TABLE trigger_catcher
                      (id INTEGER PRIMARY KEY AUTOINCREMENT);]])

            execute('ALTER TABLE t_to_rename RENAME TO t_renamed;')

            execute('DROP INDEX t_to_rename_a ON t_renamed;')

            execute([[CREATE TRIGGER t1t AFTER INSERT ON t1 FOR EACH ROW
                      BEGIN INSERT INTO trigger_catcher VALUES(1); END;]])

            _, err3 = box.execute('DROP TABLE t3;')

            execute([[CREATE TRIGGER t2t AFTER INSERT ON t2 FOR EACH ROW
                      BEGIN INSERT INTO trigger_catcher VALUES(1); END;]])

            _, err4 = box.execute('CREATE INDEX t1a ON t1(a, b);')

            execute('TRUNCATE TABLE t1;')
            execute('TRUNCATE TABLE t2;')
            _, err5 = box.execute('TRUNCATE TABLE t_does_not_exist;')

            execute('DROP TRIGGER t2t;')

            return {'Finished ok, errors in the middle: ', err1.message,
                     err2.message, err3.message, err4.message, err5.message}
        end
        local function monster_ddl_check()
            local _, err, res
            execute('INSERT INTO t2 VALUES (1, 1, 1);')
            _, err = box.execute('INSERT INTO t2 VALUES(2, 2, 1);')
            execute('INSERT INTO t1 VALUES (1, 1, 1);')
            res = execute('SELECT * FROM trigger_catcher;')
            t.assert(box.space.t_renamed ~= nil)
            t.assert_equals(box.space.t_renamed.index.t_to_rename_a, nil)
            t.assert_equals(box.space.t_to_rename, nil)
            return {'Finished ok, errors and trigger catcher content: ',
                    err.message, res}
        end
        local function monster_ddl_clear()
            execute('DROP TRIGGER IF EXISTS t1t;')
            execute('DROP TABLE IF EXISTS trigger_catcher;')
            execute('DROP TABLE IF EXISTS t2;')
            execute('DROP TABLE IF EXISTS t1;')
            execute('DROP TABLE IF EXISTS t_renamed;')
            t.assert_equals(box.space.t1, nil)
            t.assert_equals(box.space.t2, nil)
            t.assert_equals(box.space._trigger:count(), 0)
            t.assert_equals(box.space._fk_constraint:count(), 0)
            t.assert_equals(box.space._ck_constraint:count(), 0)
            t.assert_equals(box.space.t_renamed, nil)
            t.assert_equals(box.space.t_to_rename, nil)
        end

        -- No txn.
        local true_ddl_res = monster_ddl()
        local exp = {'Finished ok, errors in the middle: ',
                     "Space 't1' already exists",
                     "Space 't1' already exists",
                     "Space 't3' does not exist",
                     "Index 't1a' already exists in space 't1'",
                     "Space 't_does_not_exist' does not exist"}
        t.assert_equals(true_ddl_res, exp)

        local true_check_res = monster_ddl_check()
        exp = {'Finished ok, errors and trigger catcher content: ',
                'Duplicate key exists in unique index "unique_unnamed_t2_2" ' ..
                'in space "t2" with old tuple - [1, 1, 1] ' ..
                'and new tuple - [2, 2, 1]',
                {
                    metadata = {
                        {name = "id", type = "integer"},
                    },
                    rows = {{1}},
                }
        }
        t.assert_equals(true_check_res, exp)

        monster_ddl_clear()

        -- Both DDL and cleanup in one txn.
        box.begin()
        local ddl_res = monster_ddl()
        monster_ddl_clear()
        box.commit()
        t.assert_equals(ddl_res, true_ddl_res)

        -- DDL in txn, cleanup is not.
        box.begin()
        ddl_res = monster_ddl()
        box.commit()
        t.assert_equals(ddl_res, true_ddl_res)

        local check_res = monster_ddl_check()
        t.assert_equals(check_res, true_check_res)

        monster_ddl_clear()

        -- DDL is not in txn, cleanup is.
        ddl_res = monster_ddl()
        t.assert_equals(ddl_res, true_ddl_res)

        check_res = monster_ddl_check()
        t.assert_equals(check_res, true_check_res)

        box.begin()
        monster_ddl_clear()
        box.commit()

        -- DDL and cleanup in separate txns.
        box.begin()
        ddl_res = monster_ddl()
        box.commit()
        t.assert_equals(ddl_res, true_ddl_res)

        check_res = monster_ddl_check()
        t.assert_equals(check_res, true_check_res)

        box.begin()
        monster_ddl_clear()
        box.commit()

        -- Try SQL transactions.
        execute('START TRANSACTION;')
        ddl_res = monster_ddl()
        execute('COMMIT;')
        t.assert_equals(ddl_res, true_ddl_res)

        check_res = monster_ddl_check()
        t.assert_equals(check_res, true_check_res)

        execute('START TRANSACTION;')
        monster_ddl_clear()
        execute('COMMIT')

        execute('START TRANSACTION;')
        ddl_res = monster_ddl()
        execute('ROLLBACK;')
        t.assert_equals(ddl_res, true_ddl_res)

        -- Voluntary rollback.
        box.begin()
        execute('CREATE TABLE t1(id INTEGER PRIMARY KEY);')
        t.assert(box.space.t1 ~= nil)
        execute('CREATE TABLE t2(id INTEGER PRIMARY KEY);')
        t.assert(box.space.t2 ~= nil)
        box.rollback()

        t.assert(box.space.t1 == nil and box.space.t2 == nil)

        box.begin()
        local save1 = box.savepoint()
        execute('CREATE TABLE t1(id INTEGER PRIMARY KEY);')
        local save2 = box.savepoint()
        execute('CREATE TABLE t2(id INTEGER PRIMARY KEY, a INTEGER);')
        execute('CREATE INDEX t2a ON t2(a);')
        local save3 = box.savepoint()
        t.assert(box.space.t1 ~= nil)
        t.assert(box.space.t2 ~= nil)
        t.assert(box.space.t2.index.t2a ~= nil)
        execute('DROP TABLE t2;')
        t.assert_equals(box.space.t2, nil)
        box.rollback_to_savepoint(save3)
        t.assert(box.space.t2 ~= nil)
        t.assert(box.space.t2.index.t2a ~= nil)
        box.savepoint()
        execute('DROP TABLE t2;')
        t.assert_equals(box.space.t2, nil)
        box.rollback_to_savepoint(save2)
        t.assert_equals(box.space.t2, nil)
        t.assert(box.space.t1 ~= nil)
        box.rollback_to_savepoint(save1)
        box.commit()

        t.assert(box.space.t1 == nil and box.space.t2 == nil)

        -- Unexpected rollback.
        box.begin()
        ddl_res = monster_ddl()
        fiber.yield()
        t.assert_equals(ddl_res, true_ddl_res)
        exp_err = "Transaction has been aborted by a fiber yield"
        t.assert_error_msg_equals(exp_err, box.commit)
        box.rollback()
        monster_ddl_clear()
    end)
end
