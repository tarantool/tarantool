local t = require('luatest')
local net = require('net.box')
local server = require('luatest.server')

local g = t.group()

g.before_all(function()
    g.server = server:new({
        alias = 'master',
        box_cfg = {}
    })
    g.server:start()
    g.conn = net.connect(g.server.net_box_uri)
end)

g.after_all(function()
    g.conn:close()
    g.server:drop()
end)

g.test_tuple_format_validate = function()
    g.server:exec(function()
        local space = box.schema.space.create('test_validate', {
            format = {
                {name = 'id', type = 'string'},
                {name = 'value', type = 'any'}
            }
        })

        local valid_cases = {
            {'key', 1},
            {'key', 140},
            {'key', 0.5},
            {'key', true},
            {'key', false},
            {'', 1},
            {'key', {1, 2, 3}},
            {'key', {nested = 'value'}},
            {'key', 'value'},
            box.tuple.new({'key', 1}),
            box.tuple.new({'key', true}),
            box.tuple.new({'key', false}),
            box.tuple.new({'', 1}),
            box.tuple.new({'', 0.5}),
            box.tuple.new({'key', {1, 2, 3}}),
            box.tuple.new({'key', 'value'}),
        }

        for _, case in ipairs(valid_cases) do
            local ok, err = space.format_object:validate(case)
            if not ok then
                error("successful confirmation was expected for: " ..
                      require('json').encode(case) .. ", error: " .. (err or "no error"))
            end
        end

        local invalid_cases = {
            {1, 2},
            {true, 'a'},
            {false, {1, 2, 3}},
            box.tuple.new({1, 'a'}),
            box.tuple.new({1, 2}),
        }

        for _, case in ipairs(invalid_cases) do
            local _, err = pcall(function() space.format_object:validate(case) end)
            if not err or not tostring(err):find("expected string, got") then
                error("an error was expected for: " .. require('json').encode(case))
            end
        end
    end)
end
