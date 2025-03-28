local t = require('luatest')
local net = require('net.box')
local server = require('luatest.server')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()

    g.server:exec(function()
        if box.space.test then box.space.test:drop() end
        box.schema.space.create('test', {
            format = {
                {name = 'key', type = 'string'},
                {name = 'value', type = 'any'},
            },
        })
        box.space.test:create_index('primary', {parts = {'key'}})
    end)
    g.conn = net.connect(g.server.net_box_uri)
end)

g.after_all(function()
    g.conn:close()
    g.server:exec(function()
        if box.space.test then box.space.test:drop() end
    end)
    g.server:drop()
end)

g.before_each(function()
    g.server:exec(function()
        box.space.test:truncate()
    end)
end)

g.after_each(function()
    g.server:exec(function()
        box.space.test:truncate()
    end)
end)

g.test_tuple_format_validate = function()
    local format = box.tuple.format.new({
        {name = 'key', type = 'string'},
        {name = 'value', type = 'any'},
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
        local ok, err = format:validate(case)
        t.assert(ok, "successful confirmation was expected for: "
                .. tostring(case) .. ", error: " .. (err or "no error"))
    end

    local invalid_cases = {
        {1, 2},
        {true, 'a'},
        {false, {1, 2, 3}},
        box.tuple.new({1, 'a'}),
        box.tuple.new({1, 2}),
    }

    for _, case in ipairs(invalid_cases) do
        t.assert_error_msg_contains("expected string, got", function()
            format:validate(case)
        end, "an error was expected for: " .. tostring(case))
    end
end

g.test_space_schema_format_validate = function()
    g.server:exec(function()
        local t = require('luatest')
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
            local ok, err = pcall(space.validate, space, case)
            t.assert(ok, ("Expected success for %s, got error: %s")
                :format(require('yaml').encode(case), tostring(err)))
        end

        local invalid_cases = {
            {1, 2},
            {true, 'a'},
            {false, {1, 2, 3}},
            box.tuple.new({1, 'a'}),
            box.tuple.new({1, 2}),
        }

        for _, case in ipairs(invalid_cases) do
            local ok, err = pcall(space.validate, space, case)
            t.assert(not ok, ("Expected error for %s, but validation succeeded")
                :format(require('yaml').encode(case)))
            t.assert(tostring(err):find("field 1") or
                tostring(err):find("Field 1"),
                ("Unexpected error message for %s: %s")
                :format(require('yaml').encode(case), tostring(err)))
        end
        space:drop()
    end)
end
