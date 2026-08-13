local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

-- Check that '#' by a JSON path does not read past the updated field.
g.test_delete_by_json_path = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')

        -- The number of fields to delete is not bounded by the array.
        s:insert({1, {10, 20, 30}})
        t.assert_equals(s:update(1, {{'#', '[2][2]', 4294967295}}):totable(),
                        {1, {10}})

        -- A numeric path step matches an integer map key, whose size is
        -- not the size of a string of the same length.
        local map = setmetatable({[2] = 'v', [3] = 'w'},
                                 {__serialize = 'map'})
        s:replace({1, map})
        t.assert_equals(s:update(1, {{'#', '[2][2]', 1}}):totable(),
                        {1, {[3] = 'w'}})
    end)
end
