local yaml = require('yaml')
local fiber = require('fiber')
local t = require('luatest')

local g = t.group()

local strip = function(str)
    return str:gsub('^%s*', ''):gsub('\n%s*', '\n')
end

local function serialize(o)
    return yaml.decode(yaml.encode(o))
end

g.test_gh_8350_no_unnecessary_anchors = function()
    local x = {{}}
    setmetatable(x, {__serialize = function(_) return {x[1]} end})
    local expected = [[
        ---
        - []
        ...
    ]]
    t.assert_equals(yaml.encode(x), strip(expected))
end

g.test_gh_8310_alias_across_serialize_method = function()
    local x = {}
    local y = setmetatable({}, {__serialize = function() return x end})
    local z = {x, y}
    local expected = [[
        ---
        - &0 []
        - *0
        ...
    ]]
    t.assert_equals(yaml.encode(z), strip(expected))
end

g.test_gh_8321_alias_between_same_udata_objects = function()
    local x = serialize({fiber.self(), fiber.self()})
    t.assert(x[1] == x[2])
end
