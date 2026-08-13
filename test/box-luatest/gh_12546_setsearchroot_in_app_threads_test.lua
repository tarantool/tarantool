local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {app_threads = 1},
        net_box_credentials = {user = 'admin'}
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_setsearchroot = function(cg)
    local p1 = cg.server:exec(function()
        return package.searchroot()
    end)
    local p2 = cg.server:exec(function()
        return package.searchroot()
    end, {}, {_thread_id = 1})
    t.assert_equals(p1, p2)
    local p3 = cg.server:exec(function()
        local fio = require('fio')
        package.setsearchroot(fio.tempdir())
        return package.searchroot()
    end)
    local p4 = cg.server:exec(function()
        return package.searchroot()
    end, {}, {_thread_id = 1})
    t.assert_equals(p3, p4)
    local p5 = cg.server:exec(function(path)
        package.setsearchroot(path)
        return package.searchroot()
    end, {p1}, {_thread_id = 1})
    local p6 = cg.server:exec(function()
        return package.searchroot()
    end)
    t.assert_equals(p5, p6)
    t.assert_equals(p6, p1)
end
