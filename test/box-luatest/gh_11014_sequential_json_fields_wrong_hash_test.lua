local t = require('luatest')
local server = require('luatest.server')

local g = t.group('json_index_test')

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()

    g.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {1, 'unsigned'}})
    end)
end)


g.after_all(function()
    g.server:drop()
end)

g.after_each(function()
    g.server:exec(function()
        box.space.test:truncate()
        if box.space.test.index.sk then
            box.space.test.index.sk:drop()
        end
    end)
end)

g.test_json_index_sequential_fields = function()
    g.server:exec(function()
        local s = box.space.test

        local parts = {
            {2, 'unsigned', path = '[1]'},
            {3, 'unsigned', path = '[1]'},
        }
        local sk = s:create_index('sk', {type = 'hash', parts = parts})
        s:replace{1, {1, 2}, {3, 4}}

        local result = sk:get{1, 3}
        t.assert_equals(result, {1, {1, 2}, {3, 4}})
    end)
end

g.test_json_index_non_sequential_fields = function()
    g.server:exec(function()
        local s = box.space.test

        local parts = {
            {2, 'unsigned', path = '[1]'},
            {4, 'unsigned', path = '[1]'},
        }
        local sk = s:create_index('sk', {type = 'hash', parts = parts})
        s:replace{1, {1, 2}, {}, {3, 4}}

        local result = sk:get{1, 3}
        t.assert_equals(result, {1, {1, 2}, {}, {3, 4}})
    end)
end
