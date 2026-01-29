local fun = require('fun')

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

local function for_each_list_item(s, f)
    return table.concat(fun.iter(s:split('\n- ')):map(f):totable(), '\n- ')
end

local function for_each_paragraph(s, f)
    return table.concat(fun.iter(s:split('\n\n')):map(f):totable(), '\n\n')
end

local function format_text(s)
    return for_each_paragraph(dedent(s), function(paragraph)
        -- Strip line breaks if the paragraph is not a list.
        if paragraph:startswith('- ') then
            -- Strip newlines in each list item.
            return '- ' .. for_each_list_item(paragraph:sub(3),
                                              function(list_item)
                return toline(list_item)
            end)
        else
            return toline(paragraph)
        end
    end)
end

return {
    dedent = dedent,
    toline = toline,
    format_text = format_text,
}
