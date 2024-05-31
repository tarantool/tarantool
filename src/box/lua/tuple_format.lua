local utils = require('internal.utils')

local internal = box.internal

-- new() needs a wrapper in Lua, because format normalization needs to be done
-- in Lua.
box.tuple.format.new = function(format)
    utils.check_param(format, 'format', 'table')
    format = box.internal.space.normalize_format(nil, nil, format, 2)
    return box.internal.tuple_format.new(format)
end

setmetatable(box.tuple.format, {
    __call = function(_, t)
        return internal.tuple.tuple_get_format(t)
    end,
})
