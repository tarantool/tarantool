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

g.test_randomblob = function()
    g.server:exec(function()
        local ret, err = box.execute([[SELECT randomblob(0x80000010);]])
        t.assert(ret == nil)
        local msg = [[Failed to execute SQL statement: string or blob too big]]
        t.assert_equals(err.message, msg)
    end)
end

g.test_zeroblob = function()
    g.server:exec(function()
        local ret, err = box.execute([[SELECT zeroblob(0x80000010);]])
        t.assert(ret == nil)
        local msg = [[Failed to execute SQL statement: string or blob too big]]
        t.assert_equals(err.message, msg)
    end)
end

g.test_replace = function()
    g.server:exec(function()
        local a = string.rep('1', 50000)
        local b = string.rep('2', 50000)
        local ret, err = box.execute([[SELECT replace(?, '1', ?);]], {a, b})
        t.assert(ret == nil)
        local msg = [[Failed to execute SQL statement: string or blob too big]]
        t.assert_equals(err.message, msg)
        ret, err = box.execute([[SELECT replace(zeroblob(0x1000), zeroblob(1),
                                                zeroblob(0x10000000))]])
        t.assert(ret == nil)
        t.assert_equals(err.message, msg)
    end)
end

g.test_quote = function()
    g.server:exec(function()
        local ret, err = box.execute([[SELECT quote(zeroblob(499999999));]])
        t.assert(ret == nil)
        local msg = [[Failed to execute SQL statement: string or blob too big]]
        t.assert_equals(err.message, msg)
    end)
end

g.test_hex = function()
    g.server:exec(function()
        local ret, err = box.execute([[SELECT hex(zeroblob(500000001));]])
        t.assert(ret == nil)
        local msg = [[Failed to execute SQL statement: string or blob too big]]
        t.assert_equals(err.message, msg)
    end)
end

g.test_group_concat = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, s VARBINARY);]])
        box.execute([[INSERT INTO t VALUES(1, zeroblob(10000));]])
        box.execute([[INSERT INTO t VALUES(2, zeroblob(10000));]])
        local sql = [[SELECT group_concat(s, zeroblob(999999999))]] ..
                    [[ FROM SEQSCAN t;]]
        local ret, err = box.execute(sql)
        t.assert(ret == nil)
        local msg = [[Failed to execute SQL statement: string or blob too big]]
        t.assert_equals(err.message, msg)
    end)
end
