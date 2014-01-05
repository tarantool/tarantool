--------------------------------------------------------------------------------
-- Parameters parsing
--------------------------------------------------------------------------------

msgpack.encode()
msgpack.decode()

msgpack.encode('a', 'b')
msgpack.decode('a', 'b')

--# setopt delimiter ';'

function deepcompare(a, b)
    if type(a) == "number" or type(b) == "number" then
        return a == b
    end

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
    local x2 = msgpack.decode(msgpack.encode(x))
    xstr = type(x) == "table" and "table" or tostring(x)
    if deepcompare(x2, x) then
        return string.format("%s ok", xstr)
    else
        return string.format("%s fail, got %s", xstr, x2)
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

--------------------------------------------------------------------------------
-- Test str, bool, nil encoding / decoding 
--------------------------------------------------------------------------------

test(nil)

test(false)

test(true)

test("")
test("abcde")
test(string.rep("x", 33))

--------------------------------------------------------------------------------
-- Test tables encoding / decoding
--------------------------------------------------------------------------------

test({})

test({1, 2, 3})

test({k1 = 'v1', k2 = 'v2', k3 = 'v3'})

test({[0] = 1, 2, 3, 4, 5})
msgpack.decode(msgpack.encode({[0] = 1, 2, 3, 4, 5}))

-- test sparse / dense arrays
test({1, 2, 3, 4, 5, [10] = 10 })
msgpack.decode(msgpack.encode({1, 2, 3, 4, 5, [10] = 10}))

test({1, 2, 3, 4, 5, [100] = 100 })
msgpack.decode(msgpack.encode({1, 2, 3, 4, 5, [100] = 100}))

--------------------------------------------------------------------------------
-- Test serializer flags
--------------------------------------------------------------------------------

t1 = setmetatable({[1] = 1, [100] = 100}, {_serializer_type = "array"})
t2 = msgpack.decode(msgpack.encode(t1))
#t2
getmetatable(t2)._serializer_type

t1 = setmetatable({[1] = 1, [100] = 100}, {_serializer_type = "map"})
t2 = msgpack.decode(msgpack.encode(t1))
t2
getmetatable(t2)._serializer_type

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

--# setopt delimiter ''
-- Test  aliases, loads and dumps
a = { 1, 2, 3 }
msgpack.decode(msgpack.dumps(a))
msgpack.loads(msgpack.encode(a))
-- Test msgpack.next
dump = msgpack.dumps({1, 2, 3})..msgpack.dumps({4, 5, 6})
dump:len()
a, offset = msgpack.next(dump)
a
offset
a, offset = msgpack.next(dump, offset)
a
offset
a, offset = msgpack.next(dump, offset)
