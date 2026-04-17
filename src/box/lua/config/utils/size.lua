local M = {}

local units = {
    B = 1,
    KIB = 1024,
    MIB = 1024 * 1024,
    GIB = 1024 * 1024 * 1024,
    TIB = 1024 * 1024 * 1024 * 1024,
    PIB = 1024 * 1024 * 1024 * 1024 * 1024,
}

local function normalize_number(value)
    local number = tonumber(value)
    if number == nil then
        return nil, ('Unable to parse a number from %q'):format(value)
    end
    if number < 0 then
        return nil, ('Expected a non-negative number, got %s'):format(number)
    end
    if number - math.floor(number) ~= 0 then
        return nil, ('Expected a number without a fractional part, got %s')
            :format(number)
    end
    return number
end

function M.parse(value)
    if type(value) == 'number' then
        return normalize_number(value)
    end
    if type(value) ~= 'string' then
        return nil, ('Expected a number or a string, got %q')
            :format(type(value))
    end

    local number, unit = value:match('^%s*([%d%.]+)%s*([%a]*)%s*$')
    if number == nil then
        return nil, ('Unable to parse a byte size from %q'):format(value)
    end

    local parsed, err = normalize_number(number)
    if parsed == nil then
        return nil, err
    end

    if unit == nil or unit == '' then
        return parsed
    end

    unit = unit:upper()
    local multiplier = units[unit]
    if multiplier == nil then
        return nil, ('Unknown size suffix %q (use B, KiB, MiB, GiB, TiB, ' ..
            'PiB, or no suffix for bytes)'):format(unit)
    end
    return parsed * multiplier
end

return M
