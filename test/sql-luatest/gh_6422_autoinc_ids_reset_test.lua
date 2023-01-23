local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'test_autoinc_id_reset'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_autoinc_id_reset = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY AUTOINCREMENT, a INT);]])
        local stmt_id = box.prepare([[INSERT INTO t(a) VALUES (?);]]).stmt_id
        t.assert_equals(box.execute(stmt_id, {1}).autoincrement_ids, {1})
        t.assert_equals(box.execute(stmt_id, {1}).autoincrement_ids, {2})
        box.execute([[DROP TABLE t;]])
    end)
end
