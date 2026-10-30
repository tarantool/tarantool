-- Remove indent from a text.
--
-- Similar to Python's textwrap.dedent().
--
-- It strips all newlines from beginning and all whitespace
-- characters from the end for convenience use with multiline
-- string literals ([[ <...> ]]).
local function dedent(s)
    local lines = s:lstrip('\n'):rstrip():split('\n')

    local indent = math.huge
    for _, line in ipairs(lines) do
        if #line ~= 0 then
            indent = math.min(indent, #line:match('^ *'))
        end
    end

    local res = {}
    for _, line in ipairs(lines) do
        table.insert(res, line:sub(indent + 1))
    end
    return table.concat(res, '\n')
end

-- Ease writing of a long error message in a code.
local function toline(s)
    return s:gsub('\n', ' '):gsub(' +', ' '):strip()
end

return {
    dedent = dedent,
    toline = toline,
}
