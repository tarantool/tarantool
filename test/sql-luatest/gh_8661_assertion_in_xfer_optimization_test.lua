local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_index_field_missing = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY);]])
        box.schema.space.create('a', {format = {{'i', 'integer'}}})
        local _, ret = box.execute([[INSERT INTO t SELECT * FROM a;]])
        local err = 'SQL does not support spaces without primary key'
        t.assert_equals(ret.message, err)
    end)
end
