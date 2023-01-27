local it = require('test.interactive_tarantool')

local t = require('luatest')
local g = t.group()

g.before_each(function()
    g.child = it.new()
end)

g.after_each(function()
    if g.child ~= nil then
        g.child:close()
    end
end)

-- Verify that no private fields are shown for the 'tarantool'
-- module table at serialization to console.
g.test_no_internal_fields = function()
    -- Print content of the module table.
    g.child:execute_command("require('tarantool')")
    local response = g.child:read_response()

    -- Collect fields that start from the underscore.
    local internal_keys = {}
    for k, _ in pairs(response) do
        if k:startswith('_') then
            table.insert(internal_keys, k)
        end
    end

    -- Ensure that there are no such fields.
    t.assert_equals(internal_keys, {})
end
