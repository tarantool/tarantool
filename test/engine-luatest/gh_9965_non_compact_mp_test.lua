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
        local index_upsert_c = lib:load('index_upsert')
        rawset(_G, 'test_lib', lib)
        rawset(_G, 'space_get_c', function(space, index, key)
            return index_get_c(space.id, space.index[index].id, key)
        end)
        rawset(_G, 'space_upsert_c', function(space, index, tuple, ops)
            return index_upsert_c(space.id, space.index[index].id, tuple, ops)
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

g.test_memtx_hints = function(cg)
    t.skip_if(cg.params.engine ~= 'memtx', 'hints are only in memtx')
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', {parts = {{1, 'integer'}}, hint = true})
        local key = 1
        s:insert{key}
        local mp_keys = _G.encode_num_in_all_int_ways(key)
        for _, mp_key in pairs(mp_keys) do
            local obj = _G.msgpack.object_from_raw('\x91' .. mp_key)
            t.assert_error_msg_contains('Duplicate key', s.insert, s, obj)
            local res = _G.space_get_c(s, 'pk', obj)
            t.assert_equals(res, {key})
        end
    end)
end

g.test_mp_int_in_upsert_ops = function(cg)
    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('pk')
        --
        -- Upsert numbers codecs.
        --
        local num = 2
        local mp_nums = _G.encode_num_in_all_int_ways(num)
        for _, mp_num in pairs(mp_nums) do
            -- {{'=', 2, 2}} encoded in a weird way.
            local obj = _G.msgpack.object_from_raw(
                '\x91\x93\xa1\x3d' .. mp_num .. mp_num)
            t.assert_equals(obj:decode(), {{'=', num, num}})
            box.begin()
            _G.space_upsert_c(s, 'pk', {1}, obj)
            t.assert_equals(s:get{1}, {1})
            _G.space_upsert_c(s, 'pk', {1}, obj)
            t.assert_equals(s:get{1}, {1, 2})
            s:delete{1}
            box.commit()
        end
        --
        -- Splice numbers codecs.
        --
        local ops = {{':', 2, 2, 3, '123'}}
        local num2 = 2
        local num3 = 3
        local mp_nums2 = _G.encode_num_in_all_int_ways(num2)
        local mp_nums3 = _G.encode_num_in_all_int_ways(num3)
        t.assert_equals(#mp_nums2, #mp_nums3)
        t.assert(#mp_nums2 == 9)
        for i = 1, #mp_nums2 do
            box.begin()
            s:replace{1, 'abcdef'}
            local obj = _G.msgpack.object_from_raw(
                '\x91' .. '\x95' .. '\xa1:' ..
                mp_nums2[i] .. mp_nums2[i] .. mp_nums3[i] ..
                '\xa3123')
            t.assert_equals(obj:decode(), ops)
            _G.space_upsert_c(s, 'pk', {1}, obj)
            t.assert_equals(s:get{1}, {1, 'a123ef'})
            box.commit()
        end
    end, {cg.params.engine})
end

g.test_floating_point_numbers = function(cg)
    t.skip_if(cg.params.engine ~= 'memtx')
    cg.server:exec(function()
        local ffi = require('ffi')
        local s = box.schema.space.create('test')
        s:create_index('pk')

        local function test_index(index)
            --
            -- Float tuple.
            --
            local tuple = {0, ffi.cast('float', 14.5)}
            s:replace(tuple)
            t.assert_equals(index:get{ffi.cast('double', 14.5)}, tuple)
            t.assert_equals(index:get{ffi.cast('float', 14.5)}, tuple)
            t.assert_equals(index:get{14}, nil)
            t.assert_equals(index:get{15}, nil)
            --
            -- Double tuple.
            --
            tuple = {0, ffi.cast('double', 14.5)}
            s:replace(tuple)
            t.assert_equals(index:get{ffi.cast('double', 14.5)}, tuple)
            t.assert_equals(index:get{ffi.cast('float', 14.5)}, tuple)
            t.assert_equals(index:get{14}, nil)
            t.assert_equals(index:get{15}, nil)
            --
            -- Int tuple.
            --
            tuple = {0, 14}
            s:replace(tuple)
            t.assert_equals(index:get{ffi.cast('double', 14.5)}, nil)
            t.assert_equals(index:get{ffi.cast('float', 14.5)}, nil)
            t.assert_equals(index:get{14}, tuple)
            t.assert_equals(index:get{15}, nil)
            s:delete{0}
        end
        local sk = s:create_index('sk',  {parts = {2, 'double'}, type = 'hash'})
        test_index(sk)
        sk:drop()

        local sk = s:create_index('sk',  {parts = {2, 'double'}, type = 'tree'})
        test_index(sk)
        sk:drop()

        sk = s:create_index('sk',  {parts = {2, 'number'}, type = 'hash'})
        test_index(sk)
        sk:drop()

        sk = s:create_index('sk',  {parts = {2, 'number'}, type = 'tree'})
        test_index(sk)
        s:drop()
    end)
end

g.test_alter_hash_algo_bug = function(cg)
    t.skip_if(cg.params.engine ~= 'memtx')
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        local sk = s:create_index('sk',  {
            parts = {2, 'unsigned'}, type = 'hash'})
        local tuple = {2, 16}
        s:replace(tuple)
        -- Single-field-unsigned index used to have its own special algo for
        -- hashing. On alter it would start using another algo without
        -- rebuilding the index, and the search would stop working.
        t.assert_equals(sk:get{16}, tuple)
        sk:alter({parts = {2, 'number'}})
        t.assert_equals(sk:get{16}, tuple)
    end)
end

local function test_hash_with_num(cg, start_num)
    cg.server:exec(function(start_num)
        local function make_space_with_parts(parts)
            local s = box.space.test
            if s ~= nil then
                s:drop()
            end
            local s = box.schema.space.create('test')
            s:create_index('pk', {type = 'hash', parts = parts})
            return s
        end
        --
        -- Fast hashing, parts = {UINT}.
        --
        local s = make_space_with_parts({{1, 'unsigned'}})
        local key = start_num
        _G.check_space_ops(s, {
            key_num = key, key = {key},
            missing_key_num = key + 1, missing_key = {key + 1},
            flags = 'u',
            make_mp_key = function(mp_num) return '\x91' .. mp_num end,
        })
        --
        -- Fast hashing, parts = {STR}.
        --
        s = make_space_with_parts({{1, 'string'}})
        s:insert{'abc'}
        local obj = _G.msgpack.object_from_raw('\x91\xd9\x03abc')
        t.assert_error_msg_contains('Duplicate key', s.insert, s, obj)
        t.assert_equals(s:get{'abc'}, {'abc'})
        t.assert_equals(s:get{'abcd'}, nil)
        t.assert_equals(_G.space_get_c(s, 'pk', obj), {'abc'})
        obj = _G.msgpack.object_from_raw('\x91\xd9\x04abcd')
        t.assert_equals(_G.space_get_c(s, 'pk', obj), nil)
        --
        -- Fast hashing, parts = {UINT, UINT}.
        --
        s = make_space_with_parts({{1, 'unsigned'}, {2, 'unsigned'}})
        key = start_num
        _G.check_space_ops(s, {
            key_num = key, key = {key, key},
            missing_key_num = key + 1, missing_key = {key + 1, key + 1},
            flags = 'u',
            make_mp_key = function(mp_num)
                return '\x92' .. mp_num .. mp_num
            end,
        })
        --
        -- Fast hashing, parts = {STR, UINT}.
        --
        s = make_space_with_parts({{1, 'string'}, {2, 'unsigned'}})
        key = start_num
        _G.check_space_ops(s, {
            key_num = key, key = {'abc', key},
            missing_key_num = key + 1, missing_key = {'abc', key + 1},
            flags = 'u',
            make_mp_key = function(mp_num)
                -- The string is also suboptimal.
                return '\x92' .. '\xd9\x03abc' .. mp_num
            end,
        })
        --
        -- Fast hashing, parts = {STR, STR}.
        --
        s = make_space_with_parts({{1, 'string'}, {2, 'string'}})
        s:insert{'abc', 'defg'}
        obj = _G.msgpack.object_from_raw('\x92\xd9\x03abc\xd9\x04defg')
        t.assert_error_msg_contains('Duplicate key', s.insert, s, obj)
        t.assert_equals(s:get{'abc', 'defg'}, {'abc', 'defg'})
        t.assert_equals(s:get{'abc', 'defgh'}, nil)
        t.assert_equals(s:get{'abcd', 'defg'}, nil)
        t.assert_equals(_G.space_get_c(s, 'pk', obj), {'abc', 'defg'})
        obj = _G.msgpack.object_from_raw('\x92\xd9\x03abc\xd9\x05defgh')
        t.assert_equals(_G.space_get_c(s, 'pk', obj), nil)
        obj = _G.msgpack.object_from_raw('\x92\xd9\x04abcd\xd9\x04defg')
        t.assert_equals(_G.space_get_c(s, 'pk', obj), nil)
        --
        -- Fast hashing, parts = {UINT}.
        -- Won't allow MP_INT even when positive.
        --
        s = make_space_with_parts({{1, 'unsigned'}})
        obj = _G.msgpack.object_from_raw('\x91\xd0\xff')
        t.assert_error_msg_contains('expected unsigned, got integer',
                                    s.insert, s, obj)
        --
        -- Fast hashing, parts = {INT, UINT}.
        -- Sub-optimal MP_UINT in FIELD_INTEGER.
        --
        s = make_space_with_parts({{1, 'integer'}, {2, 'unsigned'}})
        key = start_num
        _G.check_space_ops(s, {
            key_num = key, key = {1, key},
            missing_key_num = key + 1, missing_key = {1, key + 1},
            flags = 'u',
            make_mp_key = function(mp_num)
                return '\x92' .. '\x01' .. mp_num
            end,
        })
        --
        -- Fast hashing, parts = {INT, UINT}.
        -- Negative sub-optimal MP_INT in FIELD_INTEGER.
        --
        s = make_space_with_parts({{1, 'integer'}, {2, 'unsigned'}})
        key = -start_num
        _G.check_space_ops(s, {
            key_num = key, key = {key, 1},
            missing_key_num = key - 1, missing_key = {key - 1, 1},
            flags = 'ui',
            make_mp_key = function(mp_num)
                return '\x92' .. mp_num .. '\x01'
            end,
        })
        --
        -- Fast hashing, parts = {INT, UINT}.
        -- Positive sub-optimal MP_INT in FIELD_INTEGER.
        --
        s = make_space_with_parts({{1, 'integer'}, {2, 'unsigned'}})
        key = start_num
        _G.check_space_ops(s, {
            key_num = key, key = {key, 1},
            missing_key_num = key + 1, missing_key = {key + 1, 1},
            flags = 'ui',
            make_mp_key = function(mp_num)
                return '\x92' .. mp_num .. '\x01'
            end,
        })
        --
        -- Slow hashing. Fast vs slow at the moment of writing meant that the
        -- hash algorithm was different depending on the field count and their
        -- field-types. Now it is not so anymore, the hash algo is the same
        -- regardless of the field count, but the tests remain.
        --
        make_space_with_parts = function(parts)
            local s = box.space.test
            if s ~= nil then
                s:drop()
            end
            local s = box.schema.space.create('test')
            s:create_index('pk')
            s:create_index('sk', {type = 'hash', parts = parts})
            return s
        end
        --
        -- Non-supported field types.
        --
        t.assert_error_msg_contains(
            "index 'sk' in space 'test': field type 'array'",
            make_space_with_parts, {{2, 'array'}})
        t.assert_error_msg_contains(
            "index 'sk' in space 'test': field type 'map'",
            make_space_with_parts, {{2, 'map'}})
        -- This key part used to disable the fast hashing optimizations. They
        -- were only available for several fields of trivial types with no
        -- special attributes like being nullable or having a collation.
        local part3_str_opt_killer = {3, 'string', collation = 'unicode_ci'}
        --
        -- {UINT, STR}
        --
        s = make_space_with_parts({{2, 'unsigned'}, part3_str_opt_killer})
        key = start_num
        _G.check_space_ops(s, {
            index = 'sk',
            key_num = key, key = {key, 'abc'},
            missing_key_num = key + 1, missing_key = {key + 1, 'abc'},
            tuple = {1, key, 'abc'},
            flags = 'u',
            -- The string is also suboptimal.
            make_mp_tuple = function(mp_num)
                return '\x93' .. '\x01' .. mp_num .. '\xd9\x03abc'
            end,
            make_mp_key = function(mp_num)
                return '\x92' .. mp_num .. '\xd9\x03abc'
            end,
        })
        --
        -- Cleanup.
        --
        box.space.test:drop()
    end, {start_num})
end

g.test_hash = function(cg)
    t.skip_if(cg.params.engine ~= 'memtx')
    cg.server:exec(function()
        rawset(_G, 'check_space_ops', function(s, opts)
            local index_name = opts.index or 'pk'
            local index = s.index[index_name]
            -- Most of the tests use primary index with only key fields.
            local tuple = opts.tuple or opts.key
            local make_mp_key = opts.make_mp_key
            local make_mp_tuple = opts.make_mp_tuple or make_mp_key
            s:replace(tuple)
            t.assert_equals(index:get(opts.key), tuple)
            t.assert_equals(index:get(opts.missing_key), nil)

            local mp_nums = _G.encode_num_in_all_int_ways(
                opts.key_num, opts.flags)
            for _, mp_num in pairs(mp_nums) do
                --
                -- Duplicate errors for the same number encoded in many ways.
                --
                local obj = _G.msgpack.object_from_raw(make_mp_tuple(mp_num))
                t.assert_equals(obj:decode(), tuple)
                t.assert_error_msg_contains('Duplicate key', s.insert, s, obj)
                --
                -- Key hashing works regardless of how is the number encoded.
                --
                obj = _G.msgpack.object_from_raw(make_mp_key(mp_num))
                t.assert_equals(_G.space_get_c(s, index_name, obj), tuple)
            end
            -- Make sure the missing keys aren't found, and the loop above
            -- wasn't working just because suboptimal MessagePack always renders
            -- the same hash.
            mp_nums = _G.encode_num_in_all_int_ways(
                opts.missing_key_num, opts.flags)
            for _, mp_num in pairs(mp_nums) do
                local obj = _G.msgpack.object_from_raw(make_mp_key(mp_num))
                t.assert_equals(obj:decode(), opts.missing_key)
                t.assert_equals(_G.space_get_c(s, index_name, obj), nil)
            end
        end)
    end)
    local nums = {
        -- Trivial
        0, 1,
        -- int8
        120, 127, 128, 129,
        -- uint8
        250, 254, 255, 256,
        -- int16
        32760, 32766, 32767, 32768,
        -- uint16
        65530, 65534, 65535, 65536,
        -- int32
        2147483640, 2147483646, 2147483647, 2147483648,
        -- uint32
        4294967290, 4294967294, 4294967295, 4294967296,
        -- int64
        9999999999999,
    }
    for _, num in pairs(nums) do
        test_hash_with_num(cg, num)
    end
    --
    -- Extensions are used as is. Their suboptimal encoding = incorrect encoding
    -- and the behaviour is undefined then.
    --
    cg.server:exec(function()
        local decimal = require('decimal')
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:create_index('sk', {parts = {2, 'decimal'}, type = 'hash'})
        local mp1_raw = "\xc7\x03\x01\x00\x01\x0c"
        local mp2_raw = "\xc7\x04\x01\xcc\x00\x01\x0c"
        local num = decimal.new(10)
        t.assert_equals(_G.msgpack.decode(mp1_raw), num)
        t.assert_equals(_G.msgpack.decode(mp2_raw), num)

        local mp1 = _G.msgpack.object_from_raw(mp1_raw)
        local mp2 = _G.msgpack.object_from_raw(mp2_raw)
        s:insert{1, mp1}
        s:insert{2, mp2}

        local mp1_key = _G.msgpack.object_from_raw('\x91' .. mp1_raw)
        local mp2_key = _G.msgpack.object_from_raw('\x91' .. mp2_raw)
        t.assert_equals(_G.space_get_c(s, 'sk', mp1_key), {1, num})
        t.assert_equals(_G.space_get_c(s, 'sk', mp2_key), {2, num})

        t.assert_error_msg_contains('Duplicate key', s.insert, s, {3, mp1})
        t.assert_error_msg_contains('Duplicate key', s.insert, s, {3, mp2})
    end)
end
