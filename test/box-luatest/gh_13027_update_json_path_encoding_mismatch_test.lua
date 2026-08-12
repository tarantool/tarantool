local msgpack = require('msgpack')
local t = require('luatest')

local g = t.group()

-- Delete by json path key when key in tuple is encoded not optimally.
g.test_gh_13027 = function()
    -- {1, {a = 2, b = 3}} with 'a' encoded as STR8.
    local raw = "\x92\x01\x82\xd9\x01a\x02\xa1b\x03"
    -- This construction creates [t] instead of expected t.
    local x = box.tuple.new(msgpack.object_from_raw(raw))
    local u = x:update({{'#', '[1][2].a', 1}})
    t.assert_equals(u:totable(), {{1, {b = 3}}})
end
