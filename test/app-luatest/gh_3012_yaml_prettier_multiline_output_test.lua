local compat = require('tarantool').compat
local yaml = require('yaml')
local t = require('luatest')
local g = t.group()

g.test_encode = function()
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
