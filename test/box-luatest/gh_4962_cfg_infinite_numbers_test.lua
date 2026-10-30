local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_cfg_infinite_numbers = function(cg)
    cg.server:exec(function()
        for _, val in ipairs({1 / 0, -1 / 0, 0 / 0}) do
            for opt, opt_type in pairs(box.internal.template_cfg) do
                if opt_type:find('number') then
                    t.assert_error_msg_equals(
                        "Incorrect value for option '" .. opt .. "': " ..
                        "should be a finite number",
                        box.cfg, {[opt] = val})
                end
            end
        end
    end)
end
