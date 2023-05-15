local t = require('luatest')

local g = t.group()
g.before_all(function()
    local netbox = require('net.box')

    local a = {
        b = {
                c = function() return 'c' end,
                [555] = function() return 555 end
             },
        [777] = {
                    d = {
                            [444] = function() return 444 end,
                            e = function() return 'e' end
                        },
                    [666] = function() return 666 end
                },
        [555] = function() return 555 end,
        [-1] = function() return -1 end,
        [333] = netbox.self,
        f = function() return 'f' end,
        g = netbox.self
    }
    rawset(_G, 'a', a)
end)

g.after_all(function()
    rawset(_G, 'a', nil)
end)

-- Checks that procedure resolution for Lua calls works correctly.
g.test_procedure_resolution = function()
    local netbox = require('net.box')
    local function test(proc)
        t.assert_equals(netbox.self:call(proc),
                        netbox.self:eval('return ' .. proc .. '()'))
    end

    test('a.b.c')
    test('a.b.c')
    test('a.b["c"]')
    test('a.b[\'c\']')
    test('a.b[555]')
    test('a[777].d[444]')
    test('a[777].d.e')
    test('a[777][666]')
    test('a[555]')
    test('a[555.]')
    test('a[-1]')
    test('a[333]:ping')
    test('a.f')
    test('a.g:ping')
end

-- Checks that error detection in procedure resolution for Lua calls works
-- correctly.
g.test_procedure_resolution_errors = function()
    local netbox = require('net.box')
    local function test(proc)
        t.assert_error(function() netbox.self:call(proc) end)
    end

    test('')
    test('.')
    test(':')
    test('[')
    test(']')
    test('[]')
    test('a.')
    test('l:')
    test('a.b.')
    test('a[b]')
    test('a[[]')
    test('a[[777]')
    test('a["b]')
    test('a["b\']')
    test('a[\'b]')
    test('a[\'b"]')
    test('a[\'\']')
    test('a[""]')
    test('a[\'\']')
    test('a["b""]')
    test('a["b"\']')
    test('a[\'b"\']')
    test('a["b\'"]')
    test('a[333]:')
    test('a[333]:ping:')
    test('a:[333]:ping:')
    test('a:[333]:')
    test('a[555].')
    test('a[555].')
    test('a[777].[666]')
    test('a[777]d[444]')
    test('a[777].d.[444]')
    test('a[777][666]e')
    test('a[555')
    test('a[555]..')
    test('a[555]..')
    test('a[777]..[666]')
    test('a[777].][666]')
    test('a]555[')
    test('a]555]')
    test('a]]')
    test('a[[555]')
    test('a[[555]]')
    test('a.b[c]')
end
