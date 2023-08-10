local utils = require('internal.utils')

-- new() needs a wrapper in Lua, because format normalization needs to be done
-- in Lua.
box.tuple.format.new = function(format)
    utils.check_param(format, 'format', 'table')
    format = box.internal.space.normalize_format(nil, nil, format)
    return box.internal.tuple_format.new(format)
end
