local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(g)
    g.server = server:new()
    g.server:start()
end)

g.after_each(function(g)
    g.server:drop()
end)

g.test_function_create_drop_multiple = function(g)
    g.server:exec(function()
        local BOX_FUNCTION_MAX = 32000
        local fiber = require("fiber")
        fiber.set_slice(60)
        box.begin()
        box.schema.func.create("first")
        box.schema.func.create("second")
        for _ = 1, BOX_FUNCTION_MAX + 10 do
            box.schema.func.drop("first")
            box.schema.func.create("first")
            box.schema.func.drop("second")
            box.schema.func.create("second")
        end
        box.commit()
    end)
end

g.test_sql_execute_query_func_about_to_overflow = function(g)
    g.server:exec(function()
        local fiber = require("fiber")
        fiber.set_slice(60)
        box.begin()
        local i = 1
        while true do
            local ok, err = pcall(function()
                box.schema.func.create('func'..i)
            end)
            if not ok then
                local err_msg = "Failed to create function 'func31999': " ..
                    "Max function count is reached"
                t.assert_error_msg_content_equals(
                    err_msg, function() error(err) end
                )
                break
            end
            i = i + 1
        end
        box.commit()
        -- Let's drop 2 two non-consecutive functions.
        box.schema.func.drop('func3')
        box.schema.func.drop('func16')
        -- The following query creates 2 functions.
        local _, err = box.execute([[
            CREATE TABLE t(
                i INT PRIMARY KEY CHECK (i > 10) DEFAULT (RANDOM())
            );
        ]])
        t.assert_equals(err, nil)
    end)
end
