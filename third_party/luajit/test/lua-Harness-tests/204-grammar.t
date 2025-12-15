#! /usr/bin/lua
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2010-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

--[[

=head1 Lua Grammar

=head2 Synopsis

    % prove 204-grammar.t

=head2 Description

See section "The Complete Syntax of Lua" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#8>,
L<https://www.lua.org/manual/5.2/manual.html#9>,
L<https://www.lua.org/manual/5.3/manual.html#9>,
L<https://www.lua.org/manual/5.4/manual.html#9>

=cut

--]]

require'test_assertion'
local profile = require'profile'
local has_goto = _VERSION >= 'Lua 5.2' or jit
local has_attr = _VERSION >= 'Lua 5.4'
local loadstring = loadstring or load

plan'no_plan'

do --[[ empty statement ]]
    local f, msg = loadstring [[; a = 1]]
    if _VERSION == 'Lua 5.1' and not profile.luajit_compat52 then
        matches(msg, "^[^:]+:%d+: unexpected symbol near ';'", "empty statement")
    else
        is_function(f, "empty statement")
    end

    f = loadstring [[a = 1; a = 2]]
    is_function(f)

    f, msg = loadstring [[a = 1;;; a = 2]]
    if _VERSION == 'Lua 5.1' and not profile.luajit_compat52 then
        matches(msg, "^[^:]+:%d+: unexpected symbol near ';'")
    else
        is_function(f)
    end
end

do --[[ orphan break ]]
    local f, msg = loadstring [[
function f()
    print "before"
    do
        print "inner"
        break
    end
    print "after"
end
]]
    if _VERSION == 'Lua 5.1' then
        matches(msg, "^[^:]+:%d+: no loop to break", "orphan break")
    elseif _VERSION <= 'Lua 5.3' then
        matches(msg, "^[^:]+:%d+: <break> at line 5 not inside a loop", "orphan break")
    else
        matches(msg, "^[^:]+:%d+: break outside loop at line 5", "orphan break")
    end
end

do --[[ break anywhere ]]
    local f, msg = loadstring [[
function f()
    print "before"
    while true do
        print "inner"
        break
        print "break"
    end
    print "after"
end
]]
    if _VERSION == 'Lua 5.1' and not profile.luajit_compat52 then
        matches(msg, "^[^:]+:%d+: 'end' expected %(to close 'while' at line 3%) near 'print'", "break anywhere")
    else
        is_function(f, "break anywhere")
    end

    f, msg = loadstring [[
function f()
    print "before"
    while true do
        print "inner"
        if cond then
            break
            print "break"
        end
    end
    print "after"
end
]]
    if _VERSION == 'Lua 5.1' and not profile.luajit_compat52 then
        matches(msg, "^[^:]+:%d+: 'end' expected %(to close 'if' at line 5%) near 'print'", "break anywhere")
    else
        is_function(f, "break anywhere")
    end
end

--[[ goto ]]
if has_goto then
    local f, msg = loadstring [[
::label::
    goto unknown
]]
    if jit then
        matches(msg, ":%d+: undefined label 'unknown'", "unknown goto")
    else
        matches(msg, ":%d+: no visible label 'unknown' for <goto> at line %d+", "unknown goto")
    end

    f, msg = loadstring [[
::label::
    goto label
::label::
]]
    if jit then
        matches(msg, ":%d+: duplicate label 'label'", "duplicate label")
    else
        matches(msg, ":%d+: label 'label' already defined on line %d+", "duplicate label")
    end

    f, msg = loadstring [[
::e::
    goto f
    local x
::f::
    goto e
]]
    if jit then
        matches(msg, ":%d+: <goto f> jumps into the scope of local 'x'", "bad goto")
    else
        matches(msg, ":%d+: <goto f> at line %d+ jumps into the scope of local 'x'", "bad goto")
    end

    f= loadstring [[
do
::s1:: ;
    goto s2

::s2::
    goto s3

::s3::
end
]]
    is_function(f, "goto")
else
    diag("no goto")
end

do --[[ syntax error ]]
    local f, msg = loadstring [[a = { 1, 2, 3)]]
    matches(msg, ":%d+: '}' expected near '%)'", "constructor { }")

    f, msg = loadstring [[a = (1 + 2}]]
    matches(msg, ":%d+: '%)' expected near '}'", "expr ( )")

    f, msg = loadstring [[a = f(1, 2}]]
    matches(msg, ":%d+: '%)' expected near '}'", "expr ( )")

    f, msg = loadstring [[function f () return 1]]
    matches(msg, ":%d+: 'end' expected near '?<eof>'?", "function end")

    f, msg = loadstring [[do local a = f()]]
    matches(msg, ":%d+: 'end' expected near '?<eof>'?", "do end")

    f, msg = loadstring [[for i = 1, 2 do print(i)]]
    matches(msg, ":%d+: 'end' expected near '?<eof>'?", "for end")

    f, msg = loadstring [[if true then f()]]
    matches(msg, ":%d+: 'end' expected near '?<eof>'?", "if end")

    f, msg = loadstring [[while true do f()]]
    matches(msg, ":%d+: 'end' expected near '?<eof>'?", "while end")

    f, msg = loadstring [[repeat f()]]
    matches(msg, ":%d+: 'until' expected near '?<eof>'?", "repeat until")

    f, msg = loadstring [[function f (a, 2) return a * 2 end]]
    matches(msg, ":%d+: <name> or '...' expected near '2'", "function parameter list")

    f, msg = loadstring [[a = o:m[1, 2)]]
    matches(msg, ":%d+: function arguments expected near '%['", "function argument list")

    f, msg = loadstring [[for i do print(i) end]]
    matches(msg, ":%d+: '=' or 'in' expected near 'do'", "for init")

    f, msg = loadstring [[for i = 1, 2 print(i) end]]
    matches(msg, ":%d+: 'do' expected near 'print'", "for do")

    f, msg = loadstring [[if true f() end]]
    matches(msg, ":%d+: 'then' expected near 'f'", "if then")

    f, msg = loadstring [[while true f() end]]
    matches(msg, ":%d+: 'do' expected near 'f", "while do")
end

if has_attr then
    local f, msg = load [[local foo < bar > = 'bar']]
    matches(msg, "^[^:]+:%d+: unknown attribute 'bar'")

    f, msg = load [[local foo <const> = 'bar'; foo = 'baz']]
    matches(msg, "^[^:]+:%d+: attempt to assign to const variable 'foo'")

    f, msg = load [[local foo <close> = 'bar'; foo = 'baz']]
    matches(msg, "^[^:]+:%d+: attempt to assign to const variable 'foo'")
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
