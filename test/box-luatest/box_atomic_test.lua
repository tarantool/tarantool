local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(g)
    g.server = server:new()
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.test_error_propagation = function(cg)
    cg.server:exec(function()
        local non_box_err = {x = 1}
        local ok, err = pcall(box.atomic, function() error(non_box_err) end)
        t.assert_not(ok)
        t.assert_equals(err, non_box_err)
        local box_err = box.error.new('custom', 'foo')
        local ok, err = pcall(box.atomic, function() error(box_err) end)
        t.assert_not(ok)
        t.assert_equals(err, box_err)
    end)
end
