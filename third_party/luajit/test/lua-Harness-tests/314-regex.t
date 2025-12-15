#! /usr/bin/lua
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2009-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

--[[

=head1 Lua Regex Compiler

=head2 Synopsis

    % prove 314-regex.t

=head2 Description

Tests Lua Regex

Individual tests are stored in the C<rx_*> files in the same directory;
There is one test per line: each test consists of the following
columns (separated by one *or more* tabs):

=over 4

=item pattern

The Lua regex to test.

=item target

The string that will be matched against the pattern. Use '' to indicate
an empty string.

=item result

The expected result of the match.

=item description

Description of the test.

=back

=cut

--]]

require'test_assertion'
local loadstring = loadstring or load

plan(162)

local test_files = {
    'rx_captures',
    'rx_charclass',
    'rx_metachars',
}

local todo_info = {}

if _VERSION == 'Lua 5.1' then
    if not jit then
        todo_info[130] = "printable. %g"
        --todo_info[131] = "printable. %g"
        todo_info[132] = "not printable. %G"
        --todo_info[133] = "not printable. %G"
    end
    todo_info[147] = "embedded nul. \\0"
    todo_info[149] = "embedded nul. \\0"
    todo_info[151] = "embedded nul. [^\\0]"
    todo_info[153] = "embedded nul. [^\\0]"
end

local function split (line)
    local pattern, target, result, desc = '', '', '', ''
    local idx = 1
    local c = line:sub(idx, idx)
    while (c ~= '' and c ~= "\t") do
        if (c == '"') then
            pattern = pattern .. "\\\""
        else
            pattern = pattern .. c
        end
        idx = idx + 1
        c = line:sub(idx, idx)
    end
    if pattern == "''" then
        pattern = ''
    end
    while (c ~= '' and c == "\t") do
        idx = idx + 1
        c = line:sub(idx, idx)
    end
    while (c ~= '' and c ~= "\t") do
        if (c == '"') then
            target = target .. "\\\""
        else
            target = target .. c
        end
        idx = idx + 1
        c = line:sub(idx, idx)
    end
    if target == "''" then
        target = ''
    end
    while (c ~= '' and c == "\t") do
        idx = idx + 1
        c = line:sub(idx, idx)
    end
    while (c ~= '' and c ~= "\t") do
        if c == "\\" then
            idx = idx + 1
            c = line:sub(idx, idx)
            if     c == 'f' then
                result = result .. "\f"
            elseif c == 'n' then
                result = result .. "\n"
            elseif c == 'r' then
                result = result .. "\r"
            elseif c == 't' then
                result = result .. "\t"
            elseif c == '0' then
                idx = idx + 1
                c = line:sub(idx, idx)
                if     c == '1' then
                    result = result .. "\01"
                elseif c == '2' then
                    result = result .. "\02"
                elseif c == '3' then
                    result = result .. "\03"
                elseif c == '4' then
                    result = result .. "\04"
                else
                    result = result .. "\0" .. c
                end
            elseif c == "\t" then
                result = result .. "\\"
            else
                result = result .. "\\" .. c
            end
        else
            result = result .. c
        end
        idx = idx + 1
        c = line:sub(idx, idx)
    end
    if result == "''" then
        result = ''
    end
    while (c ~= '' and c == "\t") do
        idx = idx + 1
        c = line:sub(idx, idx)
    end
    while (c ~= '' and c ~= "\t") do
        desc = desc .. c
        idx = idx + 1
        c = line:sub(idx, idx)
    end
    return pattern, target, result, desc
end

local test_number = 0
local dirname = arg[0]:gsub('([^/\\]+)$', '')
for _, filename in ipairs(test_files) do
    local f, msg = io.open(dirname .. filename, 'r')
    if f == nil then
        diag(msg)
        break
    else
        for line in f:lines() do
            if line:len() == 0 then
                break
            end
            local pattern, target, result, desc = split(line)
            test_number = test_number + 1
            if todo_info[test_number] then
                todo(todo_info[test_number])
            end
            local code = [[
                    local t = {string.match("]] .. target .. [[", "]] .. pattern .. [[")}
                    if #t== 0 then
                        return 'nil'
                    else
                        for i = 1, #t do
                            t[i] = tostring(t[i])
                        end
                        return table.concat(t, "\t")
                    end
            ]]
            local compiled
            compiled, msg = loadstring(code)
            if not compiled then
                error("can't compile : " .. code .. "\n" .. msg)
            end
            if result:sub(1, 1) == '/' then
                pattern = result:sub(2, result:len() - 1)
                error_matches(compiled, pattern, desc)
            else
                local r, out
                r, msg = pcall(function () out = compiled() end)
                if r then
                    equals(out, result, desc)
                else
                    fails(desc)
                    diag(msg)
                end
            end
        end
        f:close()
    end
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
