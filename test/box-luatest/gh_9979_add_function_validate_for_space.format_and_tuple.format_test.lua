local t = require('luatest')
local net = require('net.box')
local server = require('luatest.server')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()

    g.server:exec(function()
        box.schema.space.create('test', {
            format = {
                {name = 'key', type = 'string'},
                {name = 'value', type = 'any'},
            },
        })
        box.space.test:create_index('primary', {parts = {'key'}})
    end)
end)

g.after_all(function()
    g.server:drop()
end)

g.before_each(function()
    g.conn = net.connect(g.server.net_box_uri)
    t.assert_is_not(g.conn, nil)

    g.server:exec(function()
        box.space.test:truncate()
    end)
end)

g.after_each(function()
    g.conn:close()
end)

g.test_tuple_format_validate = function()
    local format = box.tuple.format.new({
        {name = 'key', type = 'string'},
        {name = 'value', type = 'any'},
    })

    local valid_cases = {
        {'key', 1},
        {'key', 140},
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
        local ok = pcall(function()
            format:validate(case)
        end)
        t.assert(ok, "successful confirmation was expected for: "
                 .. tostring(case))
    end

    local invalid_cases = {
        {1, 2},
        {1, 'a'},
        box.tuple.new({1, 'a'}),
        box.tuple.new({1, 2}),
    }

    for _, case in ipairs(invalid_cases) do
        t.assert_error_msg_contains("expected string, got", function()
            format:validate(case)
        end, "an error was expected for: " .. tostring(case))
    end
end

g.test_space_schema_validate = function()
    g.server:exec(function()
        local format = box.space._schema:format()

        local valid_cases = {
            {'key', 1},
            {'key', 140},
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
            local ok, err = pcall(function()
                return format:validate(case)
            end)
            assert(ok, "successful confirmation was expected for: "
                   .. tostring(case) .. ", error: " .. tostring(err))
        end

        local invalid_cases = {
            {1, 2},
            {1, 'a'},
            box.tuple.new({1, 'a'}),
            box.tuple.new({1, 2}),
        }

        for _, case in ipairs(invalid_cases) do
            t.assert_error_msg_contains("must be a", function()
                format:validate(case)
            end, "an error was expected for: " .. tostring(case))
        end
    end)
end
