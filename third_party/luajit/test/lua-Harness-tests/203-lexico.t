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

=head1 Lua Lexicography

=head2 Synopsis

    % prove 203-lexico.t

=head2 Description

See "Lua 5.3 Reference Manual", section 3.1 "Lexical Conventions",
L<https://www.lua.org/manual/5.3/manual.html#3.1>.

See section "Lexical Conventions"
L<https://www.lua.org/manual/5.1/manual.html#2.1>,
L<https://www.lua.org/manual/5.2/manual.html#3.1>,
L<https://www.lua.org/manual/5.3/manual.html#3.1>,
L<https://www.lua.org/manual/5.4/manual.html#3.1>

=cut

--]]

require'test_assertion'
local loadstring = loadstring or load
local luajit21 = jit and (jit.version_num >= 20100 or jit.version:match'^RaptorJIT')

plan'no_plan'

equals("\65", "A")
equals("\065", "A")

equals(string.byte("\a"), 7)
equals(string.byte("\b"), 8)
equals(string.byte("\f"), 12)
equals(string.byte("\n"), 10)
equals(string.byte("\r"), 13)
equals(string.byte("\t"), 9)
equals(string.byte("\v"), 11)
equals(string.byte("\\"), 92)

equals(string.len("A\0B"), 3)

do
    local f, msg = loadstring [[a = "A\300"]]
    if _VERSION == 'Lua 5.1' then
        matches(msg, "^[^:]+:%d+: .- near")
    else
        matches(msg, "^[^:]+:%d+: .- escape .- near")
    end

    f, msg = loadstring [[a = " unfinished string ]]
    matches(msg, "^[^:]+:%d+: unfinished string near")

    f, msg = loadstring [[a = " unfinished string
]]
    matches(msg, "^[^:]+:%d+: unfinished string near")

    f, msg = loadstring [[a = " unfinished string \
]]
    matches(msg, "^[^:]+:%d+: unfinished string near")

    f, msg = loadstring [[a = " unfinished string \]]
    matches(msg, "^[^:]+:%d+: unfinished string near")

    f, msg = loadstring "a = [[ unfinished long string "
    matches(msg, "^[^:]+:%d+: unfinished long string .-near")

    f, msg = loadstring "a = [== invalid long string delimiter "
    matches(msg, "^[^:]+:%d+: invalid long string delimiter near")
end

do
    local a = 'alo\n123"'
    equals('alo\n123"', a)
    equals("alo\n123\"", a)
    equals('\97lo\10\04923"', a)
    equals([[alo
123"]], a)
    equals([==[
alo
123"]==], a)
end

equals(3.0, 3)
equals(314.16e-2, 3.1416)
equals(0.31416E1, 3.1416)
equals(.3, 0.3)
equals(0xff, 255)
equals(0x56, 86)

do
    local f, msg = loadstring [[a = 12e34e56]]
    matches(msg, "^[^:]+:%d+: malformed number near")
end

--[===[
--[[
--[=[
    nested long comments
--]=]
--]]
--]===]

do
    local f, msg = loadstring "  --[[ unfinished long comment "
    matches(msg, "^[^:]+:%d+: unfinished long comment .-near")
end

if _VERSION >= 'Lua 5.2' or jit then
    _dofile'lexico52/lexico.t'
end

if _VERSION >= 'Lua 5.3' or luajit21 then
    _dofile'lexico53/lexico.t'
end

if _VERSION >= 'Lua 5.4' then
    _dofile'lexico54/lexico.t'
end

if jit and pcall(require, 'ffi') then
    _dofile'lexicojit/lexico.t'
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
