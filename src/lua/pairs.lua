local M = {}

M.builtin_pairs = rawget(_G, 'pairs')

-- A proxy for pairs function that allows to use pairs for nested table in
-- box.cfg.
local patched_pairs = function(object)
    local mt = debug.getmetatable(object)
    local iterable = (mt and mt.__name == 'box_cfg') and
                     mt.__index or
                     object
    return M.builtin_pairs(iterable)
end
rawset(_G, 'pairs', patched_pairs)

return M
