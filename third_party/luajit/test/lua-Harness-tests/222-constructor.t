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

=head1 Lua Table Constructors

=head2 Synopsis

    % prove 222-constructor.t

=head2 Description

See section "Table Constructors" in "Reference Manual"
L<https://www.lua.org/manual/5.1/manual.html#2.5.7>,
L<https://www.lua.org/manual/5.2/manual.html#3.4.8>,
L<https://www.lua.org/manual/5.3/manual.html#3.4.9>,
L<https://www.lua.org/manual/5.4/manual.html#3.4.9>

See section "Table Constructors" in "Programming in Lua".

=cut

--]]

require'test_assertion'

plan(16)

do --[[ list-style init ]]
    local days = {'Sunday', 'Monday', 'Tuesday', 'Wednesday',
                  'Thursday', 'Friday', 'Saturday'}
    equals(days[4], 'Wednesday', "list-style init")
    equals(#days, 7)
end

do
    local large = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
                    1, 2, 3, 4, 5, 6, 7, 8, 9, 0 }
    equals(#large, 100)
    large = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
              1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
              1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
              1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
              1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
              1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
              1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
              1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
              1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0,
              1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 }
    equals(#large, 200)
end

do --[[ record-style init ]]
    local a = {x=0, y=0}
    equals(a.x, 0, "record-style init")
    equals(a.y, 0)
end

do
    local w = {x=0, y=0, label='console'}
    local x = {0, 1, 2}
    w[1] = "another field"
    x.f = w
    equals(w['x'], 0, "ctor")
    equals(w[1], "another field")
    equals(x.f[1], "another field")
    w.x = nil
end

do --[[ mix record-style and list-style init ]]
    local polyline = {color='blue', thickness=2, npoints=4,
                       {x=0,   y=0},
                       {x=-10, y=0},
                       {x=-10, y=1},
                       {x=0,   y=1}
                     }
    equals(polyline[2].x, -10, "mix record-style and list-style init")
end

do
    local opnames = {['+'] = 'add', ['-'] = 'sub',
                     ['*'] = 'mul', ['/'] = 'div'}
    local i = 20; local s = '-'
    local a = {[i+0] = s, [i+1] = s..s, [i+2] = s..s..s}
    equals(opnames[s], 'sub', "ctor")
    equals(a[22], '---')
end

do
    local function f() return 10, 20 end

    array_equals({f()}, {10, 20}, "ctor")
    array_equals({'a', f()}, {'a', 10, 20})
    array_equals({f(), 'b'}, {10, 'b'})
    array_equals({'c', (f())}, {'c', 10})
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
