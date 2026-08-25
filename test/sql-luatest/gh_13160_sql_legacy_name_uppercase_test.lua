local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

-- Make sure an unquoted identifier whose uppercase form is longer than the
-- original is looked up by its legacy uppercased name.
g.test_legacy_name_longer_uppercase = function()
    g.server:exec(function()
        -- U+0390 is 2 bytes, its uppercase U+0399 U+0308 U+0301 is 6 bytes.
        local name = '\xCE\x90'
        local legacy_name = '\xCE\x99\xCC\x88\xCC\x81'
        local s = box.schema.space.create(legacy_name,
                                          {format = {{'a', 'integer'}}})
        s:create_index('pk')
        s:insert({1})
        local sql = string.format([[SELECT a FROM %s WHERE a = 1;]], name)
        local res, err = box.execute(sql)
        t.assert_equals(err, nil)
        t.assert_equals(res.rows, {{1}})
        s:drop()
    end)
end
