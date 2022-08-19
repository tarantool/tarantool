local M = {}

M.raw_print = print

-- before_cb() and after_cb() must not raise an error, must not
-- yield. It would break expectations about the print() function.
_G.print = function(...)
    if M.before_cb ~= nil then
        M.before_cb()
    end
    M.raw_print(...)
    if M.after_cb ~= nil then
        M.after_cb()
    end
end

return M
