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

g.test_exists = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE A(I1 INT PRIMARY KEY, B INT);]])
        box.execute([[CREATE TABLE B(I2 INT PRIMARY KEY, A INT);]])
        box.execute([[INSERT INTO A VALUES(1, 2);]])
        box.execute([[INSERT INTO B VALUES(1, 3);]])
        local sql = [[SELECT a.b FROM (SEQSCAN a INNER JOIN b ON i1=i2) AS c;]]
        local res = box.execute(sql).rows
        t.assert_equals(res, {{2}})
        sql = [[SELECT A.B FROM (SEQSCAN A INNER JOIN B ON I1=I2) AS C;]]
        res = box.execute(sql).rows
        t.assert_equals(res, {{2}})
    end)
end
