-- Very simple pretty-printer of tabular data.
--
-- Example:
--
-- tabulate.encode({
--     {'a', 'b', 'c'},
--     tabulate.SPACER,
--     {'d', 'e', 'f'},
--     {'g', 'h', 'i'},
-- })
--
-- ->
--
-- | a | b | c |
-- | - | - | - |
-- | d | e | f |
-- | g | h | i |

local SPACER = {}

-- Format data as a table.
--
-- Accepts an array of rows. Each row in an array of values. Each
-- value is a string.
local function encode(rows)
    -- Calculate column widths and columns amount.
    local column_widths = {}
    for _i, row in ipairs(rows) do
        for j, v in ipairs(row) do
            assert(type(v) == 'string')
            column_widths[j] = math.max(column_widths[j] or 0, #v)
        end
    end
    local column_count = #column_widths

    -- Use a table as a string buffer.
    local acc = {}

    -- Add all the values into the accumulator with proper spacing
    -- around and appropriate separators.
    for _i, row in ipairs(rows) do
        if row == SPACER then
            for j = 1, column_count do
                local width = column_widths[j]
                table.insert(acc, '| ')
                table.insert(acc, ('-'):rep(width))
                table.insert(acc, ' ')
            end
            table.insert(acc, '|\n')
        else
            for j = 1, column_count do
                assert(row[j] ~= nil)
                local width = column_widths[j]
                table.insert(acc, '| ')
                table.insert(acc, row[j]:ljust(width))
                table.insert(acc, ' ')
            end
            table.insert(acc, '|\n')
        end
    end

    return table.concat(acc)
end

return {
    SPACER = SPACER,
    encode = encode,
}
