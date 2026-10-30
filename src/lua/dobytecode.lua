local M = {}

M.dobytecode = function(...)
    local arg = {...}
    -- Check whether first argument is `-b` or `-b<opts>`.
    if string.sub(arg[1], 3, 3) ~= "" then
        -- Transform `-b<opts>` to `-<opts>`.
        arg[1] = '-' .. string.sub(arg[1], 3, #arg[1])
    else
        -- Remove `-b` from the argument list.
        table.remove(arg, 1)
    end
    require('jit.bcsave').start(unpack(arg))
end

return M
