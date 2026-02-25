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

g.test_ifnull_int = function()
    g.server:exec(function()
        local sql = [[SELECT ifnull(NULL, 1);]]
        local res, err = box.execute(sql)
        t.assert(err == nil)
        t.assert_equals(res.metadata[1].type, 'integer')
    end)
end

g.test_ifnull_number = function()
    g.server:exec(function()
        local sql = [[SELECT ifnull(1.0, -2);]]
        local res, err = box.execute(sql)
        t.assert(err == nil)
        t.assert_equals(res.metadata[1].type, 'number')
    end)
end

g.test_ifnull_decimal = function()
    g.server:exec(function()
        local sql = [[SELECT ifnull(1.2, -2.1);]]
        local res, err = box.execute(sql)
        t.assert(err == nil)
        t.assert_equals(res.metadata[1].type, 'decimal')
    end)
end

g.test_ifnull_any = function()
    g.server:exec(function()
        local sql = [[SELECT ifnull(?, ?);]]
        local res, err = box.execute(sql, {1, 2})
        t.assert(err == nil)
        t.assert_equals(res.metadata[1].type, 'any')
    end)
end

g.test_ifnull_string = function()
    g.server:exec(function()
        local sql = [[SELECT ifnull('a', 'b');]]
        local res, err = box.execute(sql)
        t.assert(err == nil)
        t.assert_equals(res.metadata[1].type, 'string')
    end)
end

g.test_ifnull_number_abs = function()
    g.server:exec(function()
        local sql = [[SELECT abs(ifnull(-1.1, -1));]]
        local res, err = box.execute(sql)
        t.assert(err == nil)
        t.assert_equals(res.metadata[1].type, 'decimal')
        t.assert_equals(res.rows, {{1.1}})
    end)
end

g.test_ifnull_param_param = function()
    g.server:exec(function()
        local sql = [[SELECT abs(ifnull(null, ?));]]
        local res, err = box.execute(sql, {-1.1})
        t.assert(err == nil)
        t.assert_equals(res.metadata[1].type, 'decimal')
        t.assert_equals(res.rows, {{1.1}})
    end)
end
