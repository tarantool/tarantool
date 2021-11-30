local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all = function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end

g.after_all = function()
    g.server:stop()
end

g.after_test('test_legacy_opts', function()
    g.server:exec(function()
        box.schema.func.drop('test', {if_exists = true})
    end)
end)

g.test_legacy_opts = function()
    -- New way: no opts sub-table.
    t.assert(g.server:exec(function()
        box.schema.func.create('test', {is_multikey = true})
        local ret = box.func.test.is_multikey
        box.schema.func.drop('test')
        return ret
    end))
    -- Value type is checked.
    t.assert_error_msg_equals(
        "Illegal parameters, options parameter 'is_multikey' should be of " ..
        "type boolean",
        function()
            g.server:exec(function()
                box.schema.func.create('test', {is_multikey = 'test'})
            end)
        end)
    -- Legacy way: with opts sub-table.
    t.assert(g.server:exec(function()
        box.schema.func.create('test', {opts = {is_multikey = true}})
        local ret = box.func.test.is_multikey
        box.schema.func.drop('test')
        return ret
    end))
end
