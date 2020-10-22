test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

box.execute([[CREATE TABLE t (b STRING PRIMARY KEY);]])
box.execute([[INSERT INTO t VALUES ('aaaaaaaaaaaa');]])
test_run:cmd("setopt delimiter ';'");
box.schema.func.create('CORRUPT_SELECT', {
    returns = 'integer',
    body = [[
        function()
            box.space.T:select()
            return 1
        end]],
    exports = {'LUA', 'SQL'},
});
box.schema.func.create('CORRUPT_GET', {
    returns = 'integer',
    body = [[
        function()
            box.space.T:get('aaaaaaaaaaaa')
            return 1
        end]],
    exports = {'LUA', 'SQL'},
});
box.schema.func.create('CORRUPT_COUNT', {
    returns = 'integer',
    body = [[
        function()
            box.space.T:count()
            return 1
        end]],
    exports = {'LUA', 'SQL'},
});
box.schema.func.create('CORRUPT_MAX', {
    returns = 'integer',
    body = [[
        function()
            box.space.T.index[0]:max()
            return 1
        end]],
    exports = {'LUA', 'SQL'},
});
box.schema.func.create('CORRUPT_MIN', {
    returns = 'integer',
    body = [[
        function()
            box.space.T.index[0]:min()
            return 1
        end]],
    exports = {'LUA', 'SQL'},
});
test_run:cmd("setopt delimiter ''");

values = {"aaaaaaaaaaaa", "aaaaaaaaaaaa"}
query = [[select %s() from t where t.b = ? and t.b <= ? order by t.b;]]
box.execute(string.format(query, 'CORRUPT_SELECT'), values)
box.execute(string.format(query, 'CORRUPT_GET'), values)
box.execute(string.format(query, 'CORRUPT_COUNT'), values)
box.execute(string.format(query, 'CORRUPT_MAX'), values)
box.execute(string.format(query, 'CORRUPT_MIN'), values)
box.execute([[DROP TABLE t;]])

box.func.CORRUPT_SELECT:drop()
box.func.CORRUPT_GET:drop()
box.func.CORRUPT_COUNT:drop()
box.func.CORRUPT_MAX:drop()
box.func.CORRUPT_MIN:drop()
