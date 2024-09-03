local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-9965-non-compact-mp', {
    {engine = 'memtx'}, {engine = 'vinyl'}
})

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {
            -- Make vinyl use the bloom-filters forcefully, always.
            vinyl_cache = 0,
        }
    })
    cg.server:start()
    cg.server:exec(function()
        rawset(_G, 'msgpack', require('msgpack'))
        local build_path = os.getenv('BUILDDIR')
        package.cpath = build_path..'/test/engine-luatest/?.so;'..
                        build_path..'/test/engine-luatest/?.dylib;'..
                        package.cpath
        local lib = box.lib.load('gh_9965')
        local index_get_c = lib:load('index_get')
        rawset(_G, 'test_lib', lib)
        rawset(_G, 'space_get_c', function(space, index, key)
            return index_get_c(space.id, space.index[index].id, key)
        end)

        rawset(_G, 'encode_num_in_all_int_ways', function(num, flags)
            local str = _G.msgpack.encode(num)
            local code = str:byte(1, 1)
            local num_body = str:sub(2)
            local all_ways = {}
            local filling
            local filler = num < 0 and '\xff' or '\x00'
            flags = flags or 'ui'
            local do_uint = flags:find('u') ~= nil
            local do_int = flags:find('i') ~= nil

            if code <= 0x7f then
                num_body = str
                assert(#num_body == 1)
            elseif code == 0xcc or code == 0xd0 then
                assert(#num_body == 1)
            elseif code == 0xcd or code == 0xd1 then
                assert(#num_body == 2)
            elseif code == 0xce or code == 0xd2 then
                assert(#num_body == 4)
            elseif code == 0xcf or code == 0xd3 then
                assert(#num_body == 8)
            else
                assert(code >= 0xe0 and code <= 0xff)
                assert(num >= -0x20 and num < 0)
                num_body = str
            end

            if #str == 1 then
                -- tiny int special case
                if (num < 0 and do_int) or (num >= 0 and do_uint) then
                    table.insert(all_ways, num_body)
                end
            end
            if num >= 0 and num <= 0xff and do_uint then
                -- uint8 as 1 byte
                filling = string.rep(filler, 1 - #num_body)
                table.insert(all_ways, '\xcc' .. filling .. num_body)
            end
            if num >= 0 and num <= 0xffff and do_uint then
                -- uint8 as 2 bytes
                filling = string.rep(filler, 2 - #num_body)
                table.insert(all_ways, '\xcd' .. filling .. num_body)
            end
            if num >= 0 and num <= 0xffffffff and do_uint then
                -- uint8 as 4 bytes
                filling = string.rep(filler, 4 - #num_body)
                table.insert(all_ways, '\xce' .. filling .. num_body)
            end
            if num >= 0 and do_uint then
                -- uint8 as 8 bytes
                filling = string.rep(filler, 8 - #num_body)
                table.insert(all_ways, '\xcf' .. filling .. num_body)
            end
            if num >= -128 and num <= 127 and do_int then
                filling = string.rep(filler, 1 - #num_body)
                table.insert(all_ways, '\xd0' .. filling .. num_body)
            end
            if num >= -32768 and num <= 32767 and do_int then
                filling = string.rep(filler, 2 - #num_body)
                table.insert(all_ways, '\xd1' .. filling .. num_body)
            end
            if num >= -2147483648 and num <= 2147483647 and do_int then
                filling = string.rep(filler, 4 - #num_body)
                table.insert(all_ways, '\xd2' .. filling .. num_body)
            end
            if num <= 9223372036854775807LL and do_int then
                filling = string.rep(filler, 8 - #num_body)
                table.insert(all_ways, '\xd3' .. filling .. num_body)
            end
            return all_ways
        end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

--
-- Test the test itself. To ensure the custom encoder indeed produces valid
-- msgpack, even though not only the most compact.
--
g.test_number_allcodec = function(cg)
    cg.server:exec(function()
        local values = {
            -- int64_min
            {num = -9223372036854775808LL,     icount = 1},
            {num = -9223372036854775808LL + 1, icount = 1},

            -- int32_min
            {num = -2147483648 - 1, icount = 1},
            {num = -2147483648,     icount = 2},
            {num = -2147483648 + 1, icount = 2},

            -- int16_min
            {num = -32768 - 1, icount = 2},
            {num = -32768,     icount = 3},
            {num = -32768 + 1, icount = 3},

            -- int8_min
            {num = -128 - 1, icount = 3},
            {num = -128,     icount = 4},
            {num = -128 + 1, icount = 4},

            -- Special 'tiny number' ranges.
            -- (-23, int8_min]
            {num = -32 - 1, icount = 4},
            -- [-32, -1]
            {num = -32,     icount = 5},
            {num = -1,      icount = 5},
            -- [0, 127]
            {num = 0,       icount = 4, ucount = 5},

            -- int8_max
            {num = 127 - 1, icount = 4, ucount = 5},
            {num = 127,     icount = 4, ucount = 5},
            {num = 127 + 1, icount = 3, ucount = 4},

            -- uint8_max
            {num = 255 - 1, icount = 3, ucount = 4},
            {num = 255,     icount = 3, ucount = 4},
            {num = 255 + 1, icount = 3, ucount = 3},

            -- int16_max
            {num = 32767 - 1, icount = 3, ucount = 3},
            {num = 32767,     icount = 3, ucount = 3},
            {num = 32767 + 1, icount = 2, ucount = 3},

            -- uint16_max
            {num = 65535 - 1, icount = 2, ucount = 3},
            {num = 65535,     icount = 2, ucount = 3},
            {num = 65535 + 1, icount = 2, ucount = 2},

            -- int32_max
            {num = 2147483647 - 1, icount = 2, ucount = 2},
            {num = 2147483647,     icount = 2, ucount = 2},
            {num = 2147483647 + 1, icount = 1, ucount = 2},

            -- uint32_max
            {num = 4294967295 - 1, icount = 1, ucount = 2},
            {num = 4294967295,     icount = 1, ucount = 2},
            {num = 4294967295 + 1, icount = 1, ucount = 1},

            -- int64_max
            {num = 9223372036854775807ULL - 1, icount = 1, ucount = 1},
            {num = 9223372036854775807ULL,     icount = 1, ucount = 1},
            {num = 9223372036854775807ULL + 1, ucount = 1},

            -- uint64_max
            {num = 18446744073709551615ULL - 1, ucount = 1},
            {num = 18446744073709551615ULL,     ucount = 1},
        }

        for _, value in pairs(values) do
            local num = value.num
            local mp_nums_all = _G.encode_num_in_all_int_ways(num)
            local mp_nums_uint = _G.encode_num_in_all_int_ways(num, 'u')
            local mp_nums_int = _G.encode_num_in_all_int_ways(num, 'i')
            local mp_nums_default = _G.encode_num_in_all_int_ways(num, 'iu')
            t.assert_equals(mp_nums_all, mp_nums_default)

            local mp_nums_all_manual = table.copy(mp_nums_uint)
            for _, n in pairs(mp_nums_int) do
                table.insert(mp_nums_all_manual, n)
            end
            t.assert_items_equals(mp_nums_all, mp_nums_all_manual)

            t.assert_equals(#mp_nums_uint, value.ucount or 0)
            t.assert_equals(#mp_nums_int, value.icount or 0)
            for __, mp_num in pairs(mp_nums_all) do
                t.assert_equals(_G.msgpack.decode(mp_num), num)
            end
        end
    end)
end

g.test_non_compact_compare_mp_uint = function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('pk')
        local key = 1
        s:insert{key}
        local mp_keys = _G.encode_num_in_all_int_ways(key, 'u')
        for _, mp_key in pairs(mp_keys) do
            local obj = _G.msgpack.object_from_raw('\x91' .. mp_key)
            t.assert_error_msg_contains('Duplicate key', s.insert, s, obj)
            local res = _G.space_get_c(s, 'pk', obj)
            t.assert_equals(res, {key})
        end
    end, {cg.params.engine})
end
