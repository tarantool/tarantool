local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

-- Casting an empty or all-whitespace string to BOOLEAN must not read past the
-- string buffer while trimming whitespace (gh-13302). Under an ASan build the
-- unbounded trim loops in str_to_bool() report a heap-buffer-overflow; here we
-- pin the visible behavior to a clean type mismatch for every blank input.
g.test_cast_empty_and_blank_string_to_boolean = function()
    g.server:exec(function()
        local blanks = {
            [[CAST('' AS BOOLEAN)]],
            [[CAST(' ' AS BOOLEAN)]],
            [[CAST('   ' AS BOOLEAN)]],
            "CAST('\t' AS BOOLEAN)",
            "CAST('\t\n ' AS BOOLEAN)",
        }
        for _, expr in ipairs(blanks) do
            local res, err = box.execute('SELECT ' .. expr .. ';')
            t.assert_equals(res, nil)
            t.assert_not_equals(err, nil)
            t.assert_equals(err.type, 'ClientError')
        end
    end)
end

-- Whitespace around a valid TRUE/FALSE keyword is still trimmed and accepted.
g.test_cast_padded_boolean_keyword = function()
    g.server:exec(function()
        local cases = {
            {[[CAST('  true ' AS BOOLEAN)]], true},
            {"CAST('\tFALSE\n' AS BOOLEAN)", false},
            {[[CAST('TRUE' AS BOOLEAN)]], true},
            {[[CAST('false' AS BOOLEAN)]], false},
        }
        for _, c in ipairs(cases) do
            local res, err = box.execute('SELECT ' .. c[1] .. ';')
            t.assert_equals(err, nil)
            t.assert_equals(res.rows, {{c[2]}})
        end
    end)
end
