local xml = require('internal.xml')

local t = require('luatest')
local g = t.group()

g.test_invalid_arg = function()
    local errmsg
    local function test(...)
        t.assert_error_msg_equals(errmsg, xml.decode, ...)
    end

    errmsg = 'expected string'
    test()
    test(123)
    test(false)
end

g.test_invalid_input = function()
    local errmsg
    local function test(input, line, column)
        t.assert_error_msg_equals(
            string.format('XML decode error at line %d, column %d: %s',
                          line, column, errmsg),
            xml.decode, input)
    end

    errmsg = 'truncated document'
    test('', 1, 1)
    test('<', 1, 2)
    test('<foo', 1, 5)
    test('<foo/', 1, 6)
    test('<foo>', 1, 6)
    test('<foo><', 1, 7)
    test('<foo></', 1, 8)
    test('<foo bar', 1, 9)
    test('<foo bar ', 1, 10)
    test('<foo bar=', 1, 10)
    test('<foo bar="', 1, 11)
    test('<foo bar=""', 1, 12)
    test('<foo bar=""/', 1, 13)
    test('<foo bar=""/', 1, 13)
    test('<foo><bar/>', 1, 12)
    test('<foo><bar></bar>', 1, 17)
    test('<foo>\n<bar>\n</bar>', 3, 7)

    errmsg = 'junk after document'
    test('<foo></foo><bar/>', 1, 12)
    test('<foo></foo>\n<bar></bar>', 2, 1)

    errmsg = 'invalid token'
    test('foo', 1, 1)
    test('"foo"', 1, 1)
    test('>foo', 1, 1)
    test('/foo', 1, 1)
    test('=foo', 1, 1)
    test('<>', 1, 2)
    test('<=>', 1, 2)
    test('</foo>', 1, 2)
    test('<"foo"/>', 1, 2)
    test('< foo/>', 1, 2)
    test('<foo=/>', 1, 5)
    test('<foo"bar"/>', 1, 5)
    test('<foo bar/>', 1, 9)
    test('<foo bar />', 1, 10)
    test('<foo bar=/>', 1, 10)
    test('<foo bar=1/>', 1, 10)
    test('<foo bar="1""2"/>', 1, 13)
    test('<foo bar="1"<"2"/>', 1, 13)
    test('<foo bar="1"baz="2"/>', 1, 13)
    test('<foo bar="1" baz="2"/ >', 1, 22)
    test('<foo bar="1" baz="2"/=>', 1, 22)
    test('<foo bar="1" baz="2"/<>', 1, 22)
    test('<foo bar="1" baz="2"/foo>', 1, 22)
    test('<foo>bar</foo>', 1, 6)
    test('<foo></foo="1">', 1, 11)
    test('<foo></foo bar="1">', 1, 12)
    test('<foo\nbar="1"\nbaz=2/>', 3, 5)

    errmsg = 'mismatched tag'
    test('<foo></bar>', 1, 11)
    test('<foo>\n<bar/>\n</bar>', 3, 6)
    test('<foo><bar>\n</foo></bar>', 2, 6)

    errmsg = 'duplicate name'
    test('<foo bar="1" bar="2"/>', 1, 17)
    test('<foo bar="1">\n<bar/>\n</foo>', 2, 5)
    test('<foo bar="1">\n<foo/>\n<bar/>\n</foo>', 3, 5)
end

g.test_decode = function()
    local expected
    local function test(input)
        t.assert_equals(xml.decode(input), expected)
    end

    expected = {foo = {{}}}
    test('<foo/>')
    test(' <foo /> ')
    test('\n<foo\n/> ')
    test('<foo></foo>')
    test('<foo > </foo >')
    test('<foo\n>\n</foo\n>')

    expected = {foo = {{bar = "123"}}}
    test('<foo bar="123"/>')
    test('<foo bar = "123"/>')
    test('<foo bar\n=\n"123"/>')
    test('<foo bar="123"></foo>')

    expected = {foo = {{bar = "123", baz = "xyz"}}}
    test('<foo bar="123" baz="xyz"/>')
    test('<foo bar="123" baz="xyz"></foo>')

    expected = {foo = {{bar = {{}}}}}
    test('<foo><bar/></foo>')
    test('<foo> <bar/> </foo>')
    test('<foo>\n<bar/>\n</foo>')
    test('<foo><bar></bar></foo>')
    test('<foo> <bar> </bar> </foo>')
    test('<foo>\n<bar>\n</bar>\n</foo>')

    expected = {foo = {{bar = {{}, {}}}}}
    test('<foo><bar/><bar/></foo>')
    test('<foo><bar/><bar></bar></foo>')
    test('<foo><bar></bar><bar></bar></foo>')

    expected = {foo = {{bar = {{buz = "1"}, {buz = "2"}}}}}
    test('<foo><bar buz="1"/><bar buz="2"/></foo>')
    test('<foo><bar buz="1"/><bar buz="2"></bar></foo>')
    test('<foo><bar buz="1"></bar><bar buz="2"></bar></foo>')

    expected = {foo = {{bar = {{}}, baz = {{}}}}}
    test('<foo><bar/><baz/></foo>')
    test('<foo><bar/><baz></baz></foo>')
    test('<foo><bar></bar><baz></baz></foo>')

    expected = {foo = {{bar = {{baz = {{}}}}}}}
    test('<foo><bar><baz/></bar></foo>')
    test('<foo><bar><baz></baz></bar></foo>')

    expected = {foo = {{bar = "xyz", baz = {{}}}}}
    test('<foo bar="xyz"><baz/></foo>')
    test('<foo bar="xyz"><baz></baz></foo>')
end
