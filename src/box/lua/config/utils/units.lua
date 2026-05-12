local units = {}

local function make_units(defs, normalize)
    local res = {}
    local names = {}
    for _, def in ipairs(defs) do
        local name, multiplier = unpack(def)
        local key = normalize ~= nil and normalize(name) or name
        res[key] = multiplier
        table.insert(names, name)
    end
    return res, table.concat(names, ', ')
end

local byte_size_unit_defs = {
    {'B', 1},
    {'KiB', 1024},
    {'MiB', 1024 * 1024},
    {'GiB', 1024 * 1024 * 1024},
    {'TiB', 1024 * 1024 * 1024 * 1024},
    {'PiB', 1024 * 1024 * 1024 * 1024 * 1024},
}

local byte_size_units, byte_size_unit_names_str =
    make_units(byte_size_unit_defs, string.upper)

function units.parse_byte_size(value)
    if type(value) ~= 'number' and type(value) ~= 'string' then
        return nil, ('Expected byte size as number or string with optional ' ..
                     'byte size suffix, got %s'):format(type(value))
    end

    local number, unit = value, nil
    if type(value) ~= 'number' then
        number, unit = value:match('^%s*([%+%-]?[%d%.]+)%s*([%a]*)%s*$')
    end

    if number == nil then
        return nil, ('Unable to parse a number from %q'):format(value)
    end

    local parser = tonumber64
    if type(number) == 'string' and number:find('.', 1, true) ~= nil then
        parser = tonumber
    end
    local parsed = parser(number)
    if parsed == nil then
        return nil, ('Unable to parse a number from %q'):format(number)
    end
    if parsed < 0 then
        return nil, ('Expected a non-negative byte size, got %s')
            :format(parsed)
    end

    local multiplier
    if unit == nil or unit == '' then
        multiplier = 1
    else
        unit = unit:upper()
        multiplier = byte_size_units[unit]
        if multiplier == nil then
            return nil, ('Unknown byte size suffix %q (use %s)')
                :format(unit, byte_size_unit_names_str)
        end
    end

    if multiplier == 1 and parsed - math.floor(parsed) ~= 0 then
        return nil, ('Expected byte size without a fractional part, got %s')
            :format(parsed)
    end
    return math.floor(parsed * multiplier)
end

return units
