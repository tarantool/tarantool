local t = require('luatest')

local g = t.group('gh-6119-msgpack-numbers',
                  t.helpers.matrix{msgpack = {require('msgpack'),
                                              require('msgpackffi')}})

-- Check that integral numbers are encoded as compact integers.
g.test_number_msgpack_compact_encode = function(cg)
    local msgpack = cg.params.msgpack
    -- one-byte pack
    for _, num in pairs{0, 1, -1, 127, -32} do
        t.assert_equals(msgpack.decode(msgpack.encode(num)), num);
        t.assert_equals(msgpack.encode(num):len(), 1, num)
    end
    -- several bytes pack
    for _, s in pairs{8, 16, 32} do
        -- positive
        local num = 2 ^ s - 1
        t.assert_equals(msgpack.decode(msgpack.encode(num)), num);
        t.assert_equals(msgpack.encode(num):len(), s/8 + 1, num)
        local num = 2 ^ s
        t.assert_equals(msgpack.decode(msgpack.encode(num)), num);
        t.assert_equals(msgpack.encode(num):len(), s/4 + 1, num)
        -- negative
        local num = -(2 ^ (s - 1))
        t.assert_equals(msgpack.decode(msgpack.encode(num)), num);
        t.assert_equals(msgpack.encode(num):len(), s/8 + 1, num)
        local num = -(2 ^ (s - 1)) - 1
        t.assert_equals(msgpack.decode(msgpack.encode(num)), num);
        t.assert_equals(msgpack.encode(num):len(), s/4 + 1, num)
    end
end

-- Check that big (by module) numbers are encoded correctly.
g.test_big_number_msgpack = function(cg)
    local msgpack = cg.params.msgpack

    -- Test number encode/decode.
    local function test(number)
        local result = msgpack.decode(msgpack.encode(number))
        t.assert_equals(result, number);
        -- cast possible cdata to number
        t.assert_equals(tonumber(result), number);
    end

    -- Decrease number by least possible decrement.
    local function decrease(number)
        local result = number
        local step = 1
        while result == number do
            result = result - step
            step = step + 1
        end
        return result
    end

    -- numbers near UINT64_MAX
    test(2^64)
    test(decrease(2^64))

    -- numbers near INT64_MIN
    test(-2^63)
    test(decrease(-2^63))
end
