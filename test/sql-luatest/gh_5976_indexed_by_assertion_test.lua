local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'test_assertion_in_indexed_by'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_assertion_in_indexed_by = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY, s STRING);]])
        box.space.T:create_index('I1', {parts = {2, 'string'}})
        box.space.T:create_index('I2', {parts = {2, 'string'}})
        local sql = [[UPDATE t INDEXED BY i2 SET s = 'a' WHERE s = 'z';]]
        t.assert_equals(box.execute(sql), {row_count = 0})
        box.execute([[DROP TABLE t;]])
    end)
end
