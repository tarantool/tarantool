local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'test_index_field_missing'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_index_field_missing = function()
    g.server:exec(function()
        local format = {{'I', 'integer'}}
        local s = box.schema.space.create('T', {format = format})
        s:create_index('i')
        box.execute('INSERT INTO t VALUES (1), (2);')
        format[2] = {'A', 'integer', is_nullable = true}
        s:format(format)
        s:create_index('a', {parts={'A'}, unique = false})
        local rows = box.execute('SELECT * FROM t WHERE a IS NULL;').rows
        t.assert_equals(rows, {{1, box.NULL}, {2, box.NULL}})
    end)
end
