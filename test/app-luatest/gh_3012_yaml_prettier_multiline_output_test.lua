local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

local function server_test_encode()
    local compat = require('tarantool').compat
    local yaml = require('yaml')

    local str = 'Title: xxx\n - Item 1\n - Item 2\n'
    local old_res = '--- "Title: xxx\\n - Item 1\\n - Item 2\\n"\n...\n'
    local new_res = '--- |\n  Title: xxx\n   - Item 1\n   - Item 2\n...\n'

    t.assert_equals(yaml.encode(str), old_res)
    compat.yaml_pretty_multiline = 'new'
    t.assert_equals(yaml.encode(str), new_res)
    compat.yaml_pretty_multiline = 'old'
    t.assert_equals(yaml.encode(str), old_res)
    compat.yaml_pretty_multiline = 'default'
end

g.test_encode = function()
    g.server = server:new{alias = 'default'}
    g.server:start()
    g.server:exec(server_test_encode)
    g.server:stop()
end
