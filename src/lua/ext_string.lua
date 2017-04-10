local ffi = require('ffi')

local err_string_arg = "bad argument #%d to '%s' (%s expecteed, got %s)"

--- Left-justify string in a field of given width.
-- Append "width - len(inp)" chars to given string. Input is never trucated.
-- @function ljust
-- @string       inp    the string
-- @int          width  at least bytes to be returned
-- @string[opt]  char   char of length 1 to fill with (" " by default)
-- @returns             result string
local function string_ljust(inp, width, char)
    if type(inp) ~= 'string' then
        error(err_string_arg:format(1, 'string.ljust', 'string', type(inp)), 2)
    end
    if type(width) ~= 'number' or width < 0 then
        error(err_string_arg:format(2, 'string.ljust', 'positive integer',
                                    type(width)), 2)
    end
    if char ~= nil and (type(char) ~= 'string' or #char ~= 1) then
        error(err_string_arg:format(2, 'string.ljust', 'char',
                                    type(width)), 2)
    end
    char = char or " "
    local delta = width - #inp
    if delta < 0 then
        return inp
    end
    return inp .. char:rep(delta)
end

--- Right-justify string in a field of given width.
-- Prepend "width - len(inp)" chars to given string. Input is never trucated.
-- @function rjust
-- @string       inp    the string
-- @int          width  at least bytes to be returned
-- @string[opt]  char   char of length 1 to fill with (" " by default)
-- @returns             result string
local function string_rjust(inp, width, char)
    if type(inp) ~= 'string' then
        error(err_string_arg:format(1, 'string.rjust', 'string', type(inp)), 2)
    end
    if type(width) ~= 'number' or width < 0 then
        error(err_string_arg:format(2, 'string.rjust', 'positive integer',
                                    type(width)), 2)
    end
    if char ~= nil and (type(char) ~= 'string' or #char ~= 1) then
        error(err_string_arg:format(2, 'string.rjust', 'char',
                                    type(width)), 2)
    end
    char = char or " "
    local delta = width - #inp
    if delta < 0 then
        return inp
    end
    return char:rep(delta) .. inp
end

--- Center string in a field of given width.
-- Prepend and append "(width - len(inp))/2" chars to given string.
-- Input is never trucated.
-- @function rjust
-- @string       inp    the string
-- @int          width  at least bytes to be returned
-- @string[opt]  char   char of length 1 to fill with (" " by default)
-- @returns             result string
local function string_center(inp, width, char)
    if type(inp) ~= 'string' then
        error(err_string_arg:format(1, 'string.center', 'string', type(inp)), 2)
    end
    if type(width) ~= 'number' or width < 0 then
        error(err_string_arg:format(2, 'string.center', 'positive integer',
                                    type(width)), 2)
    end
    if char ~= nil and (type(char) ~= 'string' or #char ~= 1) then
        error(err_string_arg:format(2, 'string.center', 'char',
                                    type(width)), 2)
    end
    char = char or " "
    local delta = width - #inp
    if delta < 0 then
        return inp
    end
    local pad_left = math.floor(delta / 2)
    local pad_right = delta - pad_left
    return char:rep(pad_left) .. inp .. char:rep(pad_right)
end

-- It'll automatically set string methods, too.
string.ljust  = string_ljust
string.rjust  = string_rjust
string.center = string_center
