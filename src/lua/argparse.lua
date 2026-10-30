local fun = require('fun')

local function parse_param_prefix(param)
    if param == nil then return nil end
    local is_long  = (param:find("^[-][-]") ~= nil)
    local is_short = not is_long and (param:find("^[-]") ~= nil)
    local is_dash  = is_short and (param:find("^[-]$") ~= nil)
    return is_long, is_short, is_dash
end

-- Determine whether a value should be provided for a parameter.
--
-- The value can be passed after '=' within the same argument or
-- in a next argument.
--
-- @param convert_to a type of the parameter
local function parameter_has_value(convert_to)
    return convert_to ~= 'boolean' and convert_to ~= 'boolean+'
end

local function result_set_add(t_out, key, val)
    if val == nil then
        table.insert(t_out, key)
    elseif t_out[key] == nil then
        t_out[key] = val
    elseif type(t_out[key]) == 'table' then
        table.insert(t_out[key], val)
    else
        t_out[key] = {t_out[key], val}
    end
end

local function err_bad_parameter_value(name, got, expected)
    assert(type(got) == 'boolean' or type(got) == 'string')
    assert(type(got) ~= expected)

    local reason
    if type(got) == 'boolean' then
        reason = ('Expected %s, got nothing'):format(expected)
    elseif not parameter_has_value(expected) then
        reason = ('No value expected, got "%s"'):format(got)
    else
        reason = ('Expected %s, got "%s"'):format(expected, got)
    end
    error(string.format('Bad value for parameter "%s". %s', name, reason))
end

local function convert_parameter_simple(name, convert_from, convert_to)
    if convert_to == 'number' then
        local converted = tonumber(convert_from)
        if converted == nil then
            return err_bad_parameter_value(name, convert_from, convert_to)
        end
        return converted
    elseif convert_to == 'boolean' then
        if type(convert_from) ~= 'boolean' then
            return err_bad_parameter_value(name, convert_from, convert_to)
        end
    elseif convert_to == 'string' then
        if type(convert_from) ~= 'string' then
            return err_bad_parameter_value(name, convert_from, convert_to)
        end
    else
        error(
            ('Bad conversion format "%s" provided for %s')
            :format(convert_to, name)
        )
    end
    return convert_from
end

local function convert_parameter(name, convert_from, convert_to)
    if convert_to == nil then
        return convert_from
    end
    if convert_to:find('+') then
        convert_to = convert_to:sub(1, -2)
        if type(convert_from) ~= 'table' then
            convert_from = { convert_from }
        end
        convert_from = fun.iter(convert_from):map(function(v)
            return convert_parameter_simple(name, v, convert_to)
        end):totable()
    else
        if type(convert_from) == 'table' then
            convert_from = table.remove(convert_from)
        end
        convert_from = convert_parameter_simple(name, convert_from, convert_to)
    end
    return convert_from
end

local function parameters_parse(t_in, options)
    local t_out, t_in = {}, t_in or {}

    -- Prepare a lookup table for options. An option name -> a
    -- type name to convert a parameter into or true (which means
    -- returning a value as is).
    local lookup = {}
    if options then
        for _, v in ipairs(options) do
            if type(v) ~= 'table' then
                v = {v}
            end
            lookup[v[1]] = (v[2] or true)
        end
    end

    local skip_param = false
    for i, v in ipairs(t_in) do
        -- we've used this parameter as value
        if skip_param == true then
            skip_param = false
            goto nextparam
        end
        local is_long, is_short, is_dash = parse_param_prefix(v)
        if not is_dash and is_short then
            local commands = v:sub(2)
            if not (commands:match("^[%a]+$")) then
                error(("bad argument #%d: ID not valid"):format(i))
            end
            for id in v:sub(2):gmatch("%a") do
                result_set_add(t_out, id, true)
            end
        elseif is_long then
            local command = v:sub(3)
            if command:find('=') then
                local key, val = command:match("^([%a_][%w_-]+)%=(.*)$")
                if key == nil or val == nil then
                    error(("bad argument #%d: ID not valid"):format(i))
                end
                -- Disallow an explicit value after '=' for a
                -- 'boolean' or 'boolean+' argument.
                if not parameter_has_value(lookup[key]) then
                    return err_bad_parameter_value(key, val, lookup[key])
                end
                result_set_add(t_out, key, val)
            else
                if command:match("^([%a_][%w_-]+)$") == nil then
                    error(("bad argument #%d: ID not valid"):format(i))
                end
                local val = true
                -- Don't consume a value after a 'boolean' or
                -- 'boolean+' argument.
                if parameter_has_value(lookup[command]) then
                    -- in case next argument is value of this key (not --arg)
                    local next_arg = t_in[i + 1]
                    local is_long, is_short, is_dash = parse_param_prefix(next_arg)
                    if is_dash then
                        skip_param = true
                    elseif is_long == false and not is_short and not is_dash then
                        val = next_arg
                        skip_param = true
                    end
                end
                result_set_add(t_out, command, val)
            end
        else
            table.insert(t_out, v)
        end
::nextparam::
    end
    if options then
        local unknown = {}
        for k, v in pairs(t_out) do
            if lookup[k] == nil and type(k) == "string" then
                table.insert(unknown, k)
            elseif type(lookup[k]) == 'string' then
                t_out[k] = convert_parameter(k, v, lookup[k])
            end
        end
        if #unknown > 0 then
            error(("unknown options: %s"):format(table.concat(unknown, ", ")))
        end
    end
    return t_out
end

return {
    parse = parameters_parse
}
