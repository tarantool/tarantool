local ffi = require('ffi')

ffi.cdef[[
    const char *
    memmem(const char *haystack, size_t haystack_len,
           const char *needle,   size_t needle_len);
    int isspace(int c);
]]

local c_char_ptr = ffi.typeof('const char *')

local memmem  = ffi.C.memmem
local isspace = ffi.C.isspace

local err_string_arg = "bad argument #%d to '%s' (%s expected, got %s)"

local function next_nonws(inp_p, inp_len)
    local cur = 0
    while isspace(inp_p[cur]) ~= 0 and cur < inp_len do
        cur = cur + 1
    end
    return cur
end

local function next_ws(inp_p, inp_len)
    local cur = 0
    while isspace(inp_p[cur]) == 0 and cur < inp_len do
        cur = cur + 1
    end
    return cur
end

local function string_split_empty(inp, max)
    local inp_p, inp_len = c_char_ptr(inp), #inp
    local rv = {}
    local pos = 0
    local inp_p_last = nil
    while true do
        if inp_p_last ~= nil and inp_p + pos ~= inp_p_last then
            local chunk_size = (inp_p + pos) - inp_p_last
            table.insert(rv, ffi.string(inp_p_last, chunk_size))
            if max then max = max - 1 end
        end
        inp_p_last = inp_p + pos
        if max == 0 and pos ~= inp_len then
            -- skip all whitespaces before last part
            inp_p_last = inp_p_last + next_nonws(inp_p + pos, inp_len - pos)
            local chunk_size = (inp_p + inp_len) - inp_p_last
            table.insert(rv, ffi.string(inp_p_last, chunk_size))
            break
        elseif pos == inp_len then
            break
        end
        -- skip all whitespaces
        pos = pos + next_nonws(inp_p + pos, inp_len - pos)
        inp_p_last = inp_p + pos
        -- find last non-whitespace charachter
        pos = pos + next_ws(inp_p + pos, inp_len - pos)
    end
    return rv
end

local function string_split_internal(inp, sep, max)
    local sep_len = #sep
    local inp_p, inp_len = c_char_ptr(inp), #inp
    local rv = {}
    while true do
        local sep_start = memmem(inp_p, inp_len, sep, sep_len)
        if sep_start == nil or max == 0 then
            local chunk_size = (c_char_ptr(inp) - inp_p) + inp_len
            table.insert(rv, ffi.string(inp_p, chunk_size))
            break
        end
        table.insert(rv, ffi.string(inp_p, sep_start - inp_p))
        if max then max = max - 1 end
        inp_p = sep_start + sep_len
    end
    return rv
end

local function string_split(inp, sep, max)
    if type(inp) ~= 'string' then
        error(err_string_arg:format(1, 'string.split', 'string', type(inp)), 2)
    end
    if sep ~= nil and type(sep) ~= 'string' then
        error(err_string_arg:format(2, 'string.split', 'string', type(sep)), 2)
    end
    if max ~= nil and (type(max) ~= 'number' or max < 0) then
        error(err_string_arg:format(3, 'string.split', 'positive integer',
                                    type(max)), 2)
    end
    if not sep then
        return string_split_empty(inp, max)
    end
    return string_split_internal(inp, sep, max)
end

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
local string = require('string')
string.split      = string_split
string.ljust      = string_ljust
string.rjust      = string_rjust
string.center     = string_center
