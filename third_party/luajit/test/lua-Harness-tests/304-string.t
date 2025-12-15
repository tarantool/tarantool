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

=head1 Lua String Library

=head2 Synopsis

    % prove 304-string.t

=head2 Description

Tests Lua String Library

See section "String Manipulation" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#5.4>,
L<https://www.lua.org/manual/5.2/manual.html#6.4>,
L<https://www.lua.org/manual/5.3/manual.html#6.4>,
L<https://www.lua.org/manual/5.4/manual.html#6.4>

=cut

]]

require'test_assertion'
local profile = require'profile'
local luajit21 = jit and (jit.version_num >= 20100 or jit.version:match'^RaptorJIT')
local has_dump53 = _VERSION >= 'Lua 5.3' or jit
local has_format_a = _VERSION >= 'Lua 5.3' or profile.has_string_format_a or jit
local has_format_p = _VERSION >= 'Lua 5.4'
local has_format_q52 = _VERSION >= 'Lua 5.2' or jit
local has_format_q53 = _VERSION >= 'Lua 5.3'
local has_format_q54 = _VERSION >= 'Lua 5.4'
local has_gmatch54 = _VERSION >= 'Lua 5.4'
local has_pack = _VERSION >= 'Lua 5.3' or (jit and jit.version:match'moonjit') or profile.pack
local has_rep52 = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local has_class_g = _VERSION >= 'Lua 5.2' or profile.luajit_compat52
local loadstring = loadstring or load

plan'no_plan'

do -- metatable
    local mt = getmetatable('ABC')
    is_table(mt, "metatable")
    is_table(mt.__index)

    if not profile.nocvts2n and _VERSION >= 'Lua 5.4' then
        is_function(mt.__add)
        is_function(mt.__div)
        is_function(mt.__idiv)
        is_function(mt.__mul)
        is_function(mt.__mod)
        is_function(mt.__pow)
        is_function(mt.__sub)
        is_function(mt.__unm)
    else
        is_nil(mt.__add)
        is_nil(mt.__div)
        is_nil(mt.__idiv)
        is_nil(mt.__mul)
        is_nil(mt.__mod)
        is_nil(mt.__pow)
        is_nil(mt.__sub)
        is_nil(mt.__unm)
    end

    equals(mt.__index.byte, string.byte)
    equals(mt.__index.char, string.char)
    equals(mt.__index.dump, string.dump)
    equals(mt.__index.find, string.find)
    equals(mt.__index.format, string.format)
    equals(mt.__index.gmatch, string.gmatch)
    equals(mt.__index.gsub, string.gsub)
    equals(mt.__index.len, string.len)
    equals(mt.__index.lower, string.lower)
    equals(mt.__index.match, string.match)
    equals(mt.__index.rep, string.rep)
    equals(mt.__index.reverse, string.reverse)
    equals(mt.__index.sub, string.sub)
    equals(mt.__index.upper, string.upper)

    if has_pack then
        equals(mt.__index.pack, string.pack)
        equals(mt.__index.packsize, string.packsize)
        equals(mt.__index.unpack, string.unpack)
    else
        is_nil(mt.__index.pack)
        is_nil(mt.__index.packsize)
        is_nil(mt.__index.unpack)
    end
end

do -- byte
    equals(string.byte('ABC'), 65, "function byte")
    equals(string.byte('ABC', 2), 66)
    equals(string.byte('ABC', -1), 67)
    equals(string.byte('ABC', 4), nil)
    equals(string.byte('ABC', 0), nil)
    array_equals({string.byte('ABC', 1, 3)}, {65, 66, 67})
    array_equals({string.byte('ABC', 1, 4)}, {65, 66, 67})

    local s = "ABC"
    equals(s:byte(2), 66, "method s:byte")
end

do -- char
    equals(string.char(65, 66, 67), 'ABC', "function char")
    equals(string.char(), '')

    error_matches(function () string.char(0, 'bad') end,
            "^[^:]+:%d+: bad argument #2 to 'char' %(number expected, got string%)",
            "function char (bad arg)")

    error_matches(function () string.char(0, 9999) end,
            "^[^:]+:%d+: bad argument #2 to 'char' %(.-value.-%)",
            "function char (invalid)")
end

do -- dump
    local signature
    if jit then
        signature = "\x1bLJ"
    elseif ravi then
        signature = "\x1bRavi"
    elseif _VERSION >= 'Lua 5.2' then
        signature = "\x1bLua"
    end

    local function add (a, b)
        return a + b
    end

    local d = string.dump(add)
    is_string(d, "function dump")
    local f = loadstring(d)
    is_function(f)
    equals(f(1, 2), 3)

    if signature then
        local sig = d:sub(1, #signature)
        equals(sig, signature)
    end

    if has_dump53 then
        local d2 = string.dump(add, true)
        is_string(d2, "function dump with strip")
        f = loadstring(d2)
        is_function(f)
        equals(f(1, 2), 3)
        not_equals(d2:len(), d:len())

        if signature then
            local sig = d2:sub(1, #signature)
            equals(sig, signature)
        end
    end

    error_matches(function () string.dump(print) end,
            "^[^:]+:%d+: unable to dump given function",
            "function dump (C function)")
end

do -- find
    local s = "hello world"
    array_equals({string.find(s, "hello")}, {1, 5}, "function find (mode plain)")
    array_equals({string.find(s, "hello", 1, true)}, {1, 5})
    array_equals({string.find(s, "hello", 1)}, {1, 5})
    equals(string.sub(s, 1, 5), "hello")
    array_equals({string.find(s, "world")}, {7, 11})
    array_equals({string.find(s, "l")}, {3, 3})
    equals(string.find(s, "lll"), nil)
    equals(string.find(s, "hello", 2, true), nil)
    array_equals({string.find(s, "world", 2, true)}, {7, 11})
    equals(string.find(s, "hello", 20), nil)

    s = "hello world"
    array_equals({string.find(s, "^h.ll.")}, {1, 5}, "function find (with regex & captures)")
    array_equals({string.find(s, "w.rld", 2)}, {7, 11})
    equals(string.find(s, "W.rld"), nil)
    array_equals({string.find(s, "^(h.ll.)")}, {1, 5, 'hello'})
    array_equals({string.find(s, "^(h.)l(l.)")}, {1, 5, 'he', 'lo'})
    s = "Deadline is 30/05/1999, firm"
    local date = "%d%d/%d%d/%d%d%d%d"
    equals(string.sub(s, string.find(s, date)), "30/05/1999")
    date = "%f[%S]%d%d/%d%d/%d%d%d%d"
    equals(string.sub(s, string.find(s, date)), "30/05/1999")

    error_matches(function () string.find(s, '%f') end,
            "^[^:]+:%d+: missing '%[' after '%%f' in pattern",
            "function find (invalid frontier)")
end

do -- format
    equals(string.format("pi = %.4f", math.pi), 'pi = 3.1416', "function format")
    local d = 5; local m = 11; local y = 1990
    equals(string.format("%02d/%02d/%04d", d, m, y), "05/11/1990")
    equals(string.format("%X %x", 126, 126), "7E 7e")
    local tag, title = "h1", "a title"
    equals(string.format("<%s>%s</%s>", tag, title, tag), "<h1>a title</h1>")

    equals(string.format('%q', 'a string with "quotes" and \n new line'), [["a string with \"quotes\" and \
 new line"]], "function format %q")

    if has_format_q52 then
        equals(string.format('%q', 'a string with \0 and \r.'), [["a string with \0 and \13."]], "function format %q")
        equals(string.format('%q', 'a string with \b and \b2'), [["a string with \8 and \0082"]], "function format %q")
    else
        equals(string.format('%q', 'a string with \0 and \r.'), [["a string with \000 and \r."]], "function format %q")
        equals(string.format('%q', 'a string with \b and \b2'), '"a string with \b and \b2"', "function format %q")
    end

    if has_format_q53 then
        equals(string.format('%q', 1.5), '0x1.8p+0', "function format %q")
        equals(string.format('%q', 7), '7', "function format %q")
    else
        equals(string.format('%q', 1.5), [["1.5"]], "function format %q")
        equals(string.format('%q', 7), [["7"]], "function format %q")
    end

    if has_format_q53 then
        equals(string.format('%q', nil), 'nil', "function format ('%q', nil)")
    elseif luajit21 then
        equals(string.format('%q', nil), [["nil"]], "function format ('%q', nil)")
    else
        error_matches(function () string.format("%q", nil) end,
                "^[^:]+:%d+: bad argument #2 to 'format' %(",
                "function format ('%q', nil)")
    end

    if has_format_q54 then
        equals(string.format('%q', 0/0), '(0/0)', "function format ('%q', NaN)")
        equals(string.format('%q', 1/0), '1e9999', "function format ('%q', +Inf)")
        equals(string.format('%q', -1/0), '-1e9999', "function format ('%q', -Inf)")

        error_matches(function () string.format("%-q", 0) end,
                "^[^:]+:%d+: specifier '%%q' cannot have modifiers",
                "function format '%-q'")
    end

    if luajit21 then
        matches(string.format('%q', {}), [[^"table: ]], "function format ('%q', {})")
    else
        error_matches(function () string.format("%q", {}) end,
                "^[^:]+:%d+: bad argument #2 to 'format' %(",
                "function format ('%q', {})")
    end

    if has_format_a then
        equals(string.format('%a', 1.5), '0x1.8p+0', "function format %a")
    end

    if has_format_p then
        equals(string.format('table: %p', string), tostring(string), "function format %p")
    end

    equals(string.format("%5s", 'foo'), '  foo', "function format (%5s)")

    if _VERSION >= 'Lua 5.3' then
        error_matches(function () string.format("%5s", "foo\0bar") end,
                "^[^:]+:%d+: bad argument #2 to 'format' %(string contains zeros%)",
                "function format format (%5s with \\0)")
    end

    equals(string.format("%s %s", 1, 2, 3), '1 2', "function format (too many arg)")

    equals(string.format("%% %c %%", 65), '% A %', "function format (%%)")

    local r = string.rep("ab", 100)
    equals(string.format("%s %d", r, r:len()), r .. " 200")

    error_matches(function () string.format("%s %s", 1) end,
            "^[^:]+:%d+: bad argument #3 to 'format' %(.-value.-%)",
            "function format (too few arg)")

    error_matches(function () string.format('%d', 'toto') end,
            "^[^:]+:%d+: bad argument #2 to 'format' %(number expected, got string%)",
            "function format (bad arg)")

    error_matches(function () string.format('%k', 'toto') end,
            "^[^:]+:%d+: invalid .- '%%k' to 'format'",
            "function format (invalid conversion)")

    if luajit21 then
        error_matches(function () string.format('%111s', 'toto') end,
                "^[^:]+:%d+: invalid option '%%111' to 'format'",
                "function format (invalid format)")
    else
        error_matches(function () string.format('%111s', 'toto') end,
                "^[^:]+:%d+: invalid format %(width or precision too long%)",
                "function format (invalid format)")

        error_matches(function () string.format('%------s', 'toto') end,
                "^[^:]+:%d+: invalid format %(repeated flags%)",
                "function format (invalid format)")
    end

    error_matches(function () string.format('pi = %.123f', math.pi) end,
            "^[^:]+:%d+: invalid ",
            "function format (invalid format)")

    error_matches(function () string.format('% 123s', 'toto') end,
            "^[^:]+:%d+: invalid ",
            "function format (invalid format)")
end

do -- gmatch
    local s = "hello"
    local output = {}
    for c in string.gmatch(s, '..') do
        table.insert(output, c)
    end
    array_equals(output, {'he', 'll'}, "function gmatch")
    if has_gmatch54 then
        output = {}
        for c in string.gmatch(s, '..', 2) do
            table.insert(output, c)
        end
        array_equals(output, {'el', 'lo'})
    end
    output = {}
    for c1, c2 in string.gmatch(s, '(.)(.)') do
        table.insert(output, c1)
        table.insert(output, c2)
    end
    array_equals(output, {'h', 'e', 'l', 'l'})
    s = "hello world from Lua"
    output = {}
    for w in string.gmatch(s, '%a+') do
        table.insert(output, w)
    end
    array_equals(output, {'hello', 'world', 'from', 'Lua'})
    s = "from=world, to=Lua"
    output = {}
    for k, v in string.gmatch(s, '(%w+)=(%w+)') do
        table.insert(output, k)
        table.insert(output, v)
    end
    array_equals(output, {'from', 'world', 'to', 'Lua'})
end

do -- gsub
    equals(string.gsub("hello world", "(%w+)", "%1 %1"), "hello hello world world", "function gsub")
    equals(string.gsub("hello world", "%w+", "%0 %0", 1), "hello hello world")
    equals(string.gsub("hello world from Lua", "(%w+)%s*(%w+)", "%2 %1"), "world hello Lua from")
    if _VERSION == 'Lua 5.1' then
        todo("not with 5.1")
    end
    error_matches(function () string.gsub("hello world", "%w+", "%e") end,
            "^[^:]+:%d+: invalid use of '%%' in replacement string",
            "function gsub (invalid replacement string)")
    equals(string.gsub("home = $HOME, user = $USER", "%$(%w+)", string.reverse), "home = EMOH, user = RESU")
    equals(string.gsub("4+5 = $return 4+5$", "%$(.-)%$", function (s) return tostring(loadstring(s)()) end), "4+5 = 9")
    local t = {name='lua', version='5.1'}
    equals(string.gsub("$name-$version.tar.gz", "%$(%w+)", t), "lua-5.1.tar.gz")

    equals(string.gsub("Lua is cute", 'cute', 'great'), "Lua is great")
    equals(string.gsub("all lii", 'l', 'x'), "axx xii")
    equals(string.gsub("Lua is great", '^Sol', 'Sun'), "Lua is great")
    equals(string.gsub("all lii", 'l', 'x', 1), "axl lii")
    equals(string.gsub("all lii", 'l', 'x', 2), "axx lii")
    equals(select(2, string.gsub("string with 3 spaces", ' ', ' ')), 3)

    array_equals({string.gsub("hello, up-down!", '%A', '.')}, {"hello..up.down.", 4})
    array_equals({string.gsub("hello, up-down!", '%A', '%%')}, {"hello%%up%down%", 4})
    local text = "hello world"
    local nvow = select(2, string.gsub(text, '[AEIOUaeiou]', ''))
    equals(nvow, 3)
    array_equals({string.gsub("one, and two; and three", '%a+', 'word')}, {"word, word word; word word", 5})
    local test = "int x; /* x */  int y; /* y */"
    array_equals({string.gsub(test, "/%*.*%*/", '<COMMENT>')}, {"int x; <COMMENT>", 1})
    array_equals({string.gsub(test, "/%*.-%*/", '<COMMENT>')}, {"int x; <COMMENT>  int y; <COMMENT>", 2})
    local s = "a (enclosed (in) parentheses) line"
    array_equals({string.gsub(s, '%b()', '')}, {"a  line", 1})

    error_matches(function () string.gsub(s, '%b(', '') end,
            "^[^:]+:%d+: .- pattern",
            "function gsub (malformed pattern)")

    array_equals({string.gsub("hello Lua!", "%a", "%0-%0")}, {"h-he-el-ll-lo-o L-Lu-ua-a!", 8})
    array_equals({string.gsub("hello Lua", "(.)(.)", "%2%1")}, {"ehll ouLa", 4})

    local function expand (str)
        return (string.gsub(str, '$(%w+)', _G))
    end
    name = 'Lua'; status= 'great'
    equals(expand("$name is $status, isn't it?"), "Lua is great, isn't it?")
    equals(expand("$othername is $status, isn't it?"), "$othername is great, isn't it?")

    function expand (str)
        return (string.gsub(str, '$(%w+)', function (n)
                                            return tostring(_G[n]), 1
                                       end))
    end
    matches(expand("print = $print; a = $a"), "^print = function: [0]?[Xx]?[builtin]*#?%x+; a = nil")

    error_matches(function () string.gsub("hello world", '(%w+)', '%2 %2') end,
            "^[^:]+:%d+: invalid capture index",
            "function gsub (invalid index)")

    error_matches(function () string.gsub("hello world", '(%w+)', true) end,
            "^[^:]+:%d+: bad argument #3 to 'gsub' %(string/function/table expected",
            "function gsub (bad type)")

    error_matches(function ()
        function expand (str)
           return (string.gsub(str, '$(%w+)', _G))
        end

        name = 'Lua'; status= true
        expand("$name is $status, isn't it?")
               end,
               "^[^:]+:%d+: invalid replacement value %(a boolean%)",
               "function gsub (invalid value)")

    local function trim (str)
        return (str:gsub('^%s*(.-)%s*$', '%1'))
    end
    equals(trim('foo'), 'foo', "gsub trim")
    equals(trim('   foo  bar  '), 'foo  bar')
end

do -- len
    equals(string.len(''), 0, "function len")
    equals(string.len('test'), 4)
    equals(string.len("a\000b\000c"), 5)
    equals(string.len('"'), 1)
end

do -- lower
    equals(string.lower('Test'), 'test', "function lower")
    equals(string.lower('TeSt'), 'test')
end

do -- match
    local s = "hello world"
    equals(string.match(s, '^hello'), 'hello', "function match")
    equals(string.match(s, 'world', 2), 'world')
    equals(string.match(s, 'World'), nil)
    array_equals({string.match(s, '^(h.ll.)')}, {'hello'})
    array_equals({string.match(s, '^(h.)l(l.)')}, {'he', 'lo'})
    local date = "Today is 17/7/1990"
    equals(string.match(date, '%d+/%d+/%d+'), '17/7/1990')
    array_equals({string.match(date, '(%d+)/(%d+)/(%d+)')}, {'17', '7', '1990'})
    equals(string.match("The number 1298 is even", '%d+'), '1298')
    local pair = "name = Anna"
    array_equals({string.match(pair, '(%a+)%s*=%s*(%a+)')}, {'name', 'Anna'})

    s = [[then he said: "it's all right"!]]
    array_equals({string.match(s, "([\"'])(.-)%1")}, {'"', "it's all right"}, "function match (back ref)")
    local p = "%[(=*)%[(.-)%]%1%]"
    s = "a = [=[[[ something ]] ]==]x]=]; print(a)"
    array_equals({string.match(s, p)}, {'=', '[[ something ]] ]==]x'})

    if has_class_g then
        equals(string.match(s, "%g"), "a", "match graphic char")
    end

    error_matches(function () string.match("hello world", "%1") end,
            "^[^:]+:%d+: invalid capture index",
            "function match invalid capture")

    error_matches(function () string.match("hello world", "%w)") end,
            "^[^:]+:%d+: invalid pattern capture",
            "function match invalid capture")
end

-- pack
if has_pack then
    equals(string.pack('b', 0x31), '\x31', "function pack")
    equals(string.pack('>b', 0x31), '\x31')
    equals(string.pack('=b', 0x31), '\x31')
    equals(string.pack('<b', 0x31), '\x31')
    equals(string.pack('>B', 0x91), '\x91')
    equals(string.pack('=B', 0x91), '\x91')
    equals(string.pack('<B', 0x91), '\x91')
    equals(string.byte(string.pack('<h', 1)), 1)
    equals(string.byte(string.pack('>h', 1):reverse()), 1)
    equals(string.byte(string.pack('<H', 1)), 1)
    equals(string.byte(string.pack('>H', 1):reverse()), 1)
    equals(string.byte(string.pack('<l', 1)), 1)
    equals(string.byte(string.pack('>l', 1):reverse()), 1)
    equals(string.byte(string.pack('<L', 1)), 1)
    equals(string.byte(string.pack('>L', 1):reverse()), 1)
    equals(string.byte(string.pack('<j', 1)), 1)
    equals(string.byte(string.pack('>j', 1):reverse()), 1)
    equals(string.byte(string.pack('<J', 1)), 1)
    equals(string.byte(string.pack('>J', 1):reverse()), 1)
    equals(string.byte(string.pack('<T', 1)), 1)
    equals(string.byte(string.pack('>T', 1):reverse()), 1)
    equals(string.byte(string.pack('<i', 1)), 1)
    equals(string.byte(string.pack('>i', 1):reverse()), 1)
    equals(string.byte(string.pack('<I', 1)), 1)
    equals(string.byte(string.pack('>I', 1):reverse()), 1)
    equals(string.pack('i1', 0):len(), 1)
    equals(string.pack('i2', 0):len(), 2)
    equals(string.pack('i4', 0):len(), 4)
    equals(string.pack('i8', 0):len(), 8)
    equals(string.pack('i16', 0):len(), 16)
    error_matches(function () string.pack('i20', 0) end,
            "^[^:]+:%d+: integral size %(20%) out of limits %[1,16%]",
            "function pack out limit")

    equals(string.pack('!2 i1 i4', 0, 0):len(), 6)
    equals(string.pack('i1 Xb i1', 0, 0):len(), 2)
    equals(string.pack('i1 x x i1', 0, 0):len(), 4)

    equals(string.pack('c3', 'foo'), 'foo')
    equals(string.pack('z', 'foo'), 'foo\0')
    equals(string.pack('c4', 'foo'), 'foo\0')   -- padding

    error_matches(function () string.pack('w', 0) end,
            "^[^:]+:%d+: invalid format option 'w'",
            "function pack invalid format")

    error_matches(function () string.pack('i1 Xz i1', 0, 0) end,
            "^[^:]+:%d+: bad argument #1 to 'pack' %(invalid next option for option 'X'%)",
            "function pack invalid next")

    error_matches(function () string.pack('i', 'foo') end,
            "^[^:]+:%d+: bad argument #2 to 'pack' %(number expected, got string%)",
            "function pack bad arg")
else
    is_nil(string.pack, "no string.pack");
end

-- packsize
if has_pack then
    equals(string.packsize('b'), 1, "function packsize")

    equals(string.packsize(''), 0, "function packsize empty")

    error_matches(function () string.packsize('z') end,
            "^[^:]+:%d+: bad argument #1 to 'packsize' %(variable%-length format%)",
            "function packsize bad arg")
else
    is_nil(string.packsize, "no string.packsize");
end

do -- rep
    equals(string.rep('ab', 3), 'ababab', "function rep")
    equals(string.rep('ab', 0), '')
    equals(string.rep('ab', -1), '')
    equals(string.rep('', 5), '')
    if has_rep52 then
        equals(string.rep('ab', 3, ','), 'ab,ab,ab', "with sep")
        local n = 1e6
        equals(string.rep('a', n), string.rep('', n + 1, 'a'))
    else
        diag("no rep with separator")
    end

    if _VERSION >= 'Lua 5.3' then
        error_matches(function () string.rep('foo', 1e9) end,
                "^[^:]+:%d+: resulting string too large",
                "too large")
    elseif luajit21 then
        error_equals(function () string.rep('foo', 1e9) end,
                "not enough memory",
                "too large")
    end

    if _VERSION >= 'Lua 5.4' or jit then
        equals(string.rep('', 1e8), '', "rep ''")
    else
        diag('too slow')
    end
end

do -- reverse
    equals(string.reverse('abcde'), 'edcba', "function reverse")
    equals(string.reverse('abcd'), 'dcba')
    equals(string.reverse(''), '')
end

do -- sub
    equals(string.sub('abcde', 1, 2), 'ab', "function sub")
    equals(string.sub('abcde', 3, 4), 'cd')
    equals(string.sub('abcde', -2), 'de')
    equals(string.sub('abcde', 3, 2), '')
end

do -- upper
    equals(string.upper('Test'), 'TEST', "function upper")
    equals(string.upper('TeSt'), 'TEST')
    equals(string.upper(string.rep('Test', 10000)), string.rep('TEST', 10000))
end

-- unpack
if has_pack then
    equals(string.unpack('<h', string.pack('>h', 1):reverse()), 1, "function unpack")
    equals(string.unpack('<H', string.pack('>H', 1):reverse()), 1)
    equals(string.unpack('<l', string.pack('>l', 1):reverse()), 1)
    equals(string.unpack('<L', string.pack('>L', 1):reverse()), 1)
    equals(string.unpack('<j', string.pack('>j', 1):reverse()), 1)
    equals(string.unpack('<J', string.pack('>J', 1):reverse()), 1)
    equals(string.unpack('<T', string.pack('>T', 1):reverse()), 1)
    equals(string.unpack('<i', string.pack('>i', 1):reverse()), 1)
    equals(string.unpack('<I', string.pack('>I', 1):reverse()), 1)
    equals(string.unpack('<f', string.pack('>f', 1.0):reverse()), 1.0)
    equals(string.unpack('<d', string.pack('>d', 1.0):reverse()), 1.0)
    equals(string.unpack('<n', string.pack('>n', 1.0):reverse()), 1.0)

    equals(string.unpack('c3', string.pack('c3', 'foo')), 'foo')
    equals(string.unpack('z', string.pack('z', 'foo')), 'foo')
    equals(string.unpack('s', string.pack('s', 'foo')), 'foo')

    error_matches(function () string.unpack('c4', 'foo') end,
            "^[^:]+:%d+: bad argument #2 to 'unpack' %(data string too short%)",
            "function unpack data too short")

    error_matches(function () string.unpack('c', 'foo') end,
            "^[^:]+:%d+: missing size for format option 'c'",
            "function unpack missing size")
else
    is_nil(string.unpack, "no string.unpack");
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
