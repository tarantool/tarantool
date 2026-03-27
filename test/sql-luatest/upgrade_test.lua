local server = require('luatest.server')
local t = require('luatest')

local g = t.group('upgrade', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_test('test_upgrade_from_1_10_15', function(cg)
    cg.server = server:new({datadir = 'test/sql-luatest/upgrade/1.10/'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_default_engine" = '%s';]], {engine})
        box.schema.upgrade()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end, {cg.params.engine})
end)

g.after_test('test_upgrade_from_1_10_15', function(cg)
    cg.server:drop()
end)

g.test_upgrade_from_1_10_15 = function(cg)
    cg.server:exec(function()
        box.schema.upgrade()
        -- test system tables
        local exp = {
            328, 1, '_trigger', 'memtx', 0, {},
            {
                {
                    name = 'name',
                    type = 'string',
                },
                {
                    name = 'space_id',
                    type = 'unsigned',
                },
                {
                    name = 'opts',
                    type = 'map',
                },
            },
        }
        local res = box.space._space.index['name']:get('_trigger')
        t.assert_equals(res, exp)

        exp = {
            328, 0, 'primary', 'tree',
            {unique = true},
            {{0, 'string'}},
        }
        res = box.space._index:get(
            {box.space._space.index['name']:get('_trigger').id, 0}
        )
        t.assert_equals(res, exp)

        exp = {
            {
                type = 'string',
                name = 'key',
            },
            {
                type = 'any',
                name = 'value',
                is_nullable = true,
            },
        }
        res = box.space._schema:format()
        t.assert_equals(res, exp)

        -- test data migration
        exp = {
            512, 1, 'T1', 'memtx', 0, {},
            {
                {
                    name = 'x',
                    type = 'unsigned',
                },
            },
        }
        res = box.space._space.index['name']:get('T1')
        t.assert_equals(res, exp)

        exp = {
            512, 0, 'primary', 'tree',
            {unique = true},
            {{0, 'unsigned'}},
        }
        res = box.space._index:get(
            {box.space._space.index['name']:get('T1').id, 0}
        )
        t.assert_equals(res, exp)

        -- test system tables functionality
        local sql = [[CREATE TABLE T(X INTEGER PRIMARY KEY);]]
        box.execute(sql)
        sql = [[CREATE TABLE T_OUT(X INTEGER PRIMARY KEY);]]
        box.execute(sql)
        sql = 'CREATE TRIGGER T1T AFTER INSERT ON T '..
              'FOR EACH ROW BEGIN INSERT INTO '..
              'T_OUT VALUES(1); END;'
        box.execute(sql)
        sql = 'CREATE TRIGGER T2T AFTER INSERT ON T '..
              'FOR EACH ROW BEGIN INSERT INTO '..
              'T_OUT VALUES(2); END;'
        box.execute(sql)

        exp = {
            513, 0, 'T', 'memtx', 1, {},
            {
                {
                    type = 'integer',
                    nullable_action = 'abort',
                    name = 'X',
                    is_nullable = false,
                },
            },
        }
        res = box.space._space.index['name']:get('T')
        t.assert_equals(res, exp)

        exp = {
            514, 0, 'T_OUT', 'memtx', 1, {},
            {
                {
                    type = 'integer',
                    nullable_action = 'abort',
                    name = 'X',
                    is_nullable = false,
                },
            },
        }
        res = box.space._space.index['name']:get('T_OUT')
        t.assert_equals(res, exp)
        local t1t = box.space._trigger:get('T1T')
        local t2t = box.space._trigger:get('T2T')
        t.assert_equals(t1t.name, "T1T")

        exp = {
            sql = 'CREATE TRIGGER T1T AFTER INSERT ON T '..
                  'FOR EACH ROW BEGIN INSERT INTO '..
                  'T_OUT VALUES(1); END;'
        }
        t.assert_equals(t1t.opts, exp)

        t.assert_equals(t2t.name, "T2T")

        exp = {
            sql = 'CREATE TRIGGER T2T AFTER INSERT ON T '..
                  'FOR EACH ROW BEGIN INSERT INTO '..
                  'T_OUT VALUES(2); END;'
        }
        t.assert_equals(t2t.opts, exp)
        t.assert_equals(t1t.space_id, t2t.space_id)
        t.assert_equals(t1t.space_id, box.space.T.id)

        box.execute([[INSERT INTO T VALUES(1);]])
        t.assert_equals(box.space.T:select(), {{1}})
        t.assert_equals(box.space.T_OUT:select(), {{1}, {2}})
        t.assert_equals(box.execute([[SELECT * FROM T;]]).rows, {{1}})
        t.assert_equals(box.execute([[SELECT * FROM T;]]).rows, {{1}})

        box.execute([[DROP TABLE T;]])
        box.execute([[DROP TABLE T_OUT;]])
    end)
end

g.before_test('test_upgrade_from_2_1_1', function(cg)
    cg.server = server:new({datadir = 'test/sql-luatest/upgrade/2.1.1/'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_default_engine" = '%s']], {engine})
        box.schema.upgrade()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end, {cg.params.engine})
end)

g.after_test('test_upgrade_from_2_1_1', function(cg)
    cg.server:drop()
end)

g.test_upgrade_from_2_1_1 = function(cg)
    cg.server:exec(function()
        local s = box.space.T5
        t.assert_not_equals(s, nil)
        local i = box.space._index:select(s.id)
        t.assert_not_equals(i, nil)
        t.assert_equals(i[1].opts.sql, nil)
        t.assert_equals(box.space._space:get(s.id).flags.checks, nil)
        local name, func_id = next(box.space[s.id].constraint)
        t.assert_equals(name, "CK_CONSTRAINT_1_T5")
        s:drop()
        t.assert_equals(box.space._func:delete(func_id).body, 'x < 2')
    end)
end
