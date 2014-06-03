--------------------------------------------------------------------------------
-- Parameters parsing
--------------------------------------------------------------------------------

ffi = require('ffi')
msgpack = require('msgpack')
msgpackffi = require('msgpackffi')

msgpack.encode()
msgpack.decode()

msgpack.encode('a', 'b')
msgpack.decode('a', 'b')

--# setopt delimiter ';'

function deepcompare(a, b)
    if type(a) == "number" or type(b) == "number" then
        return a == b
    end

    if ffi.istype('bool', a) then a = (a == 1) end
    if ffi.istype('bool', b) then b = (b == 1) end

    if a == nil and b == nil then return true end
    if type(a) ~= type(b) then return false end

    if type(a) == "table" then
        for k in pairs(a) do
            if not deepcompare(a[k], b[k]) then return false end
        end
        for k in pairs(b) do
            if not deepcompare(a[k], b[k]) then return false end
        end

        return true
    else
        return a == b
    end
end;

function test(x)
    local buf1 = msgpack.encode(x)
    local buf2 = msgpackffi.encode(x)
    local x1, offset1 = msgpack.decode(buf1)
    local x2, offset2 = msgpackffi.decode_unchecked(buf2)
    local xstr
    if type(x) == "table" then
        xstr = "table"
    elseif ffi.istype('float', x) then
        xstr = string.format('%0.2f (ffi float)', tonumber(x))
    elseif ffi.istype('double', x) then
        xstr = string.format('%0.2f (ffi double)', tonumber(x))
    elseif ffi.istype("bool", x) then
        xstr = string.format("%s (ffi bool)", x == 1 and "true" or "false")
    else
        xstr = tostring(x)
    end
    if #buf1 ~= #buf2 then
        return string.format("%s fail, length mismatch", xstr)
    elseif offset1 ~= #buf1 + 1 then
        return string.format("%s fail, invalid offset", xstr)
    elseif offset2 ~= #buf2 + 1 then
        return string.format("%s fail, invalid offset (ffi)", xstr)
    elseif not deepcompare(x1, x) then
        return string.format("%s fail, invalid result %s", xstr, x1)
    elseif not deepcompare(x2, x) then
        return string.format("%s fail, invalid result (ffi) %s", xstr, x2)
    else
        return string.format("%s ok", xstr)
    end
end;

--# setopt delimiter ''

--------------------------------------------------------------------------------
-- Test uint, int, double encoding / decoding
--------------------------------------------------------------------------------

--
-- unsigned int
--

test(0)
test(0LL)
test(0ULL)

test(1)
test(1LL)
test(1ULL)

test(4294967294)
test(4294967294LL)
test(4294967294ULL)

test(4294967295)
test(4294967295LL)
test(4294967295ULL)

test(4294967296)
test(4294967296LL)
test(4294967296ULL)

test(4294967297)
test(4294967297LL)
test(4294967297ULL)

test(9007199254740992)
test(9007199254740992LL)
test(9007199254740992ULL)

test(9223372036854775807LL)
test(9223372036854775807ULL)

test(9223372036854775808ULL)
test(9223372036854775809ULL)
test(18446744073709551614ULL)
test(18446744073709551615ULL)

test(-1ULL)

--
-- signed int
--

test(-1)
test(-1LL)

test(-2147483647)
test(-2147483647LL)

test(-9007199254740992)
test(-9007199254740992LL)

test(-9223372036854775806LL)

test(-9223372036854775807LL)

--
-- double
--

test(-1.1)

test(3.14159265358979323846)

test(-3.14159265358979323846)

test(-1e100)
test(1e100)
test(ffi.new('float', 123456))
test(ffi.new('double', 123456))
test(ffi.new('float', 12.121))
test(ffi.new('double', 12.121))

--------------------------------------------------------------------------------
-- Test str, bool, nil encoding / decoding 
--------------------------------------------------------------------------------

test(nil)

test(ffi.cast('void *', 0))

test(false)

test(true)

test(ffi.new('bool', true))
test(ffi.new('bool', false))

test("")
test("abcde")
test(string.rep("x", 33))

--------------------------------------------------------------------------------
-- Test tables encoding / decoding
--------------------------------------------------------------------------------

test({})

test({1, 2, 3})

test({k1 = 'v1', k2 = 'v2', k3 = 'v3'})

msgpack.decode(msgpack.encode({[0] = 1, 2, 3, 4, 5}))

-- test sparse / dense arrays
msgpack.decode(msgpack.encode({1, 2, 3, 4, 5, [10] = 10}))

msgpack.decode(msgpack.encode({1, 2, 3, 4, 5, [100] = 100}))

msgpackffi.decode_unchecked(msgpackffi.encode({1, 2, 3, 4, 5, [100] = 100}))

--------------------------------------------------------------------------------
-- Test serializer flags
--------------------------------------------------------------------------------

t1 = {[1] = 1, [100] = 100}
t2 = msgpack.decode(msgpack.encode(t1))
#t2

t1 = {[1] = 1, [100] = 100}
t2 = msgpack.decode(msgpack.encode(t1))
t2

--------------------------------------------------------------------------------
-- Test resursive tables
--------------------------------------------------------------------------------

--# setopt delimiter ';'

a = {1, 2, 3}
b = {'a', 'b', 'c'}
a[4] = b
b[4] = a;

a;
msgpack.decode(msgpack.encode(a));
msgpackffi.decode_unchecked(msgpackffi.encode(a));

--# setopt delimiter ''
-- Test  aliases, loads and dumps
a = { 1, 2, 3 }
(msgpack.decode(msgpack.dumps(a)))
(msgpack.loads(msgpack.encode(a)))
-- Test msgpack.decode with offsets
dump = msgpack.dumps({1, 2, 3})..msgpack.dumps({4, 5, 6})
dump:len()
a, offset = msgpack.decode(dump)
a
offset
a, offset = msgpack.decode(dump, offset)
a
offset
a, offset = msgpack.decode(dump, offset)

-- Test decode with offset
dump = msgpackffi.encode({1, 2, 3})..msgpackffi.encode({4, 5, 6})
dump:len()
a, offset = msgpackffi.decode_unchecked(dump)
a
offset
a, offset = msgpackffi.decode_unchecked(dump, offset)
a
offset
a, offset = msgpackffi.decode_unchecked(dump, offset)
