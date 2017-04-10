local ffi = require('ffi')

ffi.cdef[[
    const char *
    memmem(const char *haystack, size_t haystack_len,
           const char *needle,   size_t needle_len);
    int memcmp(const char *mem1, const char *mem2, size_t num);
    int isspace(int c);
]]

local c_char_ptr = ffi.typeof('const char *')

local memcmp  = ffi.C.memcmp
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
-- @function center
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

-- For now the best way to check, that string starts with sequence
-- (with patterns disabled) is to cut line and check strings for equality

--- Check that string (or substring) starts with given string
-- Optionally restricting the matching with the given offsets
-- @function startswith
-- @string    inp     original string
-- @string    head    the substring to check against
-- @int[opt]  _start  start index of matching boundary
-- @int[opt]  _end    end index of matching boundary
-- @returns           boolean
local function string_startswith(inp, head, _start, _end)
    if type(inp) ~= 'string' then
        error(err_string_arg:format(1, 'string.startswith', 'string',
                                    type(inp)), 2)
    end
    if type(head) ~= 'string' then
        error(err_string_arg:format(2, 'string.startswith', 'string',
                                    type(inp)), 2)
    end
    if _start ~= nil and type(_start) ~= 'number' then
        error(err_string_arg:format(3, 'string.startswith', 'integer',
                                    type(inp)), 2)
    end
    if _end ~= nil and type(_end) ~= 'number' then
        error(err_string_arg:format(4, 'string.startswith', 'integer',
                                    type(inp)), 2)
    end
    -- prepare input arguments (move negative values [offset from the end] to
    -- positive ones and/or assign default values)
    local head_len, inp_len = #head, #inp
    if _start == nil then
        _start = 1
    elseif _start < 0 then
        _start = inp_len + _start + 1
        if _start < 0 then _start = 0 end
    end
    if _end == nil or _end > inp_len then
        _end = inp_len
    elseif _end < 0 then
        _end = inp_len + _end + 1
        if _end < 0 then _end = 0 end
    end
    -- check for degenerate case (interval lesser than input)
    if head_len == 0 then
        return true
    elseif _end - _start + 1 < head_len or _start > _end then
        return false
    end
    _start = _start - 1
    _end = _start + head_len - 1
    return memcmp(c_char_ptr(inp) + _start, c_char_ptr(head), head_len) == 0
end

--- Check that string (or substring) ends with given string
-- Optionally restricting the matching with the given offsets
-- @function endswith
-- @string    inp     original string
-- @string    tail    the substring to check against
-- @int[opt]  _start  start index of matching boundary
-- @int[opt]  _end    end index of matching boundary
-- @returns           boolean
local function string_endswith(inp, tail, _start, _end)
    local tail_len, inp_len = #tail, #inp
    if type(inp) ~= 'string' then
        error(err_string_arg:format(1, 'string.endswith', 'string',
                                    type(inp)), 2)
    end
    if type(tail) ~= 'string' then
        error(err_string_arg:format(2, 'string.endswith', 'string',
                                    type(inp)), 2)
    end
    if _start ~= nil and type(_start) ~= 'number' then
        error(err_string_arg:format(3, 'string.endswith', 'integer',
                                    type(inp)), 2)
    end
    if _end ~= nil and type(_end) ~= 'number' then
        error(err_string_arg:format(4, 'string.endswith', 'integer',
                                    type(inp)), 2)
    end
    -- prepare input arguments (move negative values [offset from the end] to
    -- positive ones and/or assign default values)
    if _start == nil then
        _start = 1
    elseif _start < 0 then
        _start = inp_len + _start + 1
        if _start < 0 then _start = 0 end
    end
    if _end == nil or _end > inp_len then
        _end = inp_len
    elseif _end < 0 then
        _end = inp_len + _end + 1
        if _end < 0 then _end = 0 end
    end
    -- check for degenerate case (interval lesser than input)
    if tail_len == 0 then
        return true
    elseif _end - _start + 1 < tail_len or _start > _end  then
        return false
    end
    _start = _end - tail_len
    return memcmp(c_char_ptr(inp) + _start, c_char_ptr(tail), tail_len) == 0
end

-- It'll automatically set string methods, too.
local string = require('string')
string.split      = string_split
string.ljust      = string_ljust
string.rjust      = string_rjust
string.center     = string_center
string.startswith = string_startswith
string.endswith   = string_endswith
