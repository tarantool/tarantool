local server = require('luatest.server')
local t = require('luatest')

local g = t.group("upgrade_110", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({datadir = 'test/sql/upgrade/1.10/'})
    cg.server:start()
    cg.server:exec(function()
        local ffi = require('ffi')
        ffi.cdef([[
            int box_schema_upgrade_begin(void);
            void box_schema_upgrade_end(void);
        ]])
        rawset(_G, 'builtins', ffi.C)
        box.schema.upgrade()
    end)
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_default_engine" = '%s']], {engine})
        box.schema.upgrade()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_upgrade_110 = function(cg)
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
        res = box.space._index:get({box.space._space.index['name']:get('_trigger').id, 0})
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
        res = box.space._index:get({box.space._space.index['name']:get('T1').id, 0})
        t.assert_equals(res, exp)

        -- test system tables functionality
        box.execute([[CREATE TABLE T(X INTEGER PRIMARY KEY);]])
        box.execute([[CREATE TABLE T_OUT(X INTEGER PRIMARY KEY);]])
        box.execute([[CREATE TRIGGER T1T AFTER INSERT ON T FOR EACH ROW BEGIN INSERT INTO T_OUT VALUES(1); END;]])
        box.execute([[CREATE TRIGGER T2T AFTER INSERT ON T FOR EACH ROW BEGIN INSERT INTO T_OUT VALUES(2); END;]])

        exp = {
            513, 1, 'T', 'memtx', 1, {},
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
            514, 1, 'T_OUT', 'memtx', 1, {},
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
            sql = 'CREATE TRIGGER T1T AFTER INSERT ON T FOR EACH ROW BEGIN INSERT INTO T_OUT VALUES(1); END;'
        }
        t.assert_equals(t1t.opts, exp)

        t.assert_equals(t2t.name, "T2T")

        exp = {
            sql = 'CREATE TRIGGER T2T AFTER INSERT ON T FOR EACH ROW BEGIN INSERT INTO T_OUT VALUES(2); END;'
        }
        t.assert_equals(t2t.opts, exp)
        t.assert_equals(t1t.space_id, t2t.space_id)
        t.assert_equals(t1t.space_id, box.space.T.id)

        box.execute([[INSERT INTO T VALUES(1);]])
        t.assert_equals(box.space.T:select(), {1})
        t.assert_equals(box.space.T_OUT:select(), {{1}, {2}})
        t.assert_equals(box.execute([[SELECT * FROM T;]]).rows, {1})
        t.assert_equals(box.execute([[SELECT * FROM T;]]).rows, {1})

        box.execute([[DROP TABLE T;]])
        box.execute([[DROP TABLE T_OUT;]])
    end)
end
