local t = require('luatest')
local g = t.group('gh-5940')
local server = require('luatest.server')

g.before_all(function()
    g.server = server:new{alias = 'default'}
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

g.test_ddl_ops = function()
    g.server:exec(function()
        local t = require('luatest')
        local field_types = { '', 'n', 'nu', 's', 'st' }
        local error_msg = 'Wrong space format: field 1 has unknown field type'

        for _, type in pairs(field_types) do
            t.assert_error_msg_content_equals(error_msg, function()
                box.schema.create_space('a', {format = {[1] = {name = 'id', type = type}}})
            end)
        end
    end)
end
