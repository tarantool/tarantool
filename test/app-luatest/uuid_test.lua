local uuid = require('uuid')
local t = require('luatest')

local g = t.group()

g.test_rfc4122 = function()
    local u = uuid.new()
    t.assert_equals(bit.band(u.clock_seq_hi_and_reserved, 0xc0), 0x80,
                    '<uuid.new> always generates RFC4122 variant')
    local version = bit.rshift(u.time_hi_and_version, 12)
    -- `uuidgen()` syscall is used under FreeBSD to generate UUID.
    -- It always generates the v1 UUID.
    t.assert(version == 1 or version == 4,
             '<uuid.new> generates time-based or random-based version')
end

g.test_to_or_from_string = function()
    local u = uuid()
    local ustr = u:str()
    t.assert_equals(#ustr, 36)
    t.assert(ustr:match('^[a-f0-9%-]+$'))
    t.assert_equals(u, uuid.fromstr(ustr))

    local udef = 'ba90d815-14e0-431d-80c0-ce587885bb78'
    local uval = uuid.fromstr(udef)
    t.assert_equals(uval:str(), udef)
    t.assert_equals(tostring(uval), udef)
    t.assert_equals(uval:str(), tostring(uval))

    t.assert_equals(uval.time_low, 3130054677)
    t.assert_equals(uval.time_mid, 5344)
    t.assert_equals(uval.time_hi_and_version, 17181)
    t.assert_equals(uval.clock_seq_hi_and_reserved, 128)
    t.assert_equals(uval.clock_seq_low, 192)
    t.assert_equals(uval.node[0], 206)
    t.assert_equals(uval.node[1], 88)
    t.assert_equals(uval.node[2], 120)
    t.assert_equals(uval.node[3], 133)
    t.assert_equals(uval.node[4], 187)
    t.assert_equals(uval.node[5], 120)

    t.assert_equals(#uuid.str(), 36)
end

g.test_to_or_from_binary = function()
    local u = uuid()
    t.assert_equals(#u:bin(), 16)
    t.assert_equals(#u:bin('h'), 16)
    t.assert_equals(#u:bin('l'), 16)
    t.assert_equals(#u:bin('n'), 16)
    t.assert_equals(#u:bin('b'), 16)
    t.assert_equals(u:bin(), u:bin('h'))
    t.assert_not_equals(u:bin('n'), u:bin('h'))
    t.assert_not_equals(u:bin('b'), u:bin('l'))
    t.assert_equals(u, uuid.frombin(u:bin()))
    t.assert_equals(u, uuid.frombin(u:bin('b'), 'b'))
    t.assert_equals(u, uuid.frombin(u:bin('l'), 'l'))

    local udef = {
        arg = 'adf9d02e-0756-11e4-b5cf-525400123456',
        binl = '2ed0f9ad5607e411b5cf525400123456',
        binb = 'adf9d02e075611e4b5cf525400123456',
    }
    local uval = uuid.fromstr(udef.arg)
    t.assert_equals(string.hex(uval:bin('l')), udef.binl)
    t.assert_equals(string.hex(uval:bin('b')), udef.binb)

    t.assert_equals(#uuid.bin(), 16)
    t.assert_equals(#uuid.bin('l'), 16)
end

g.test_eq_and_nil = function()
    local u = uuid.new()
    t.assert_equals(uuid.NULL,
                    uuid.fromstr('00000000-0000-0000-0000-000000000000'))
    t.assert(uuid.NULL:isnil())
    t.assert_not_equals(u, uuid.NULL)
    t.assert_not(u:isnil())
    t.assert_equals(u, u)
    t.assert_not_equals(u, nil)
    t.assert_not_equals(u, 12345)
    t.assert_not_equals(u, 'blablabla')
end

g.test_invalid_usage = function()
    local u = uuid.new()
    t.assert_error_msg_equals('Usage: uuid:isnil()', u.isnil)
    t.assert_error_msg_equals('Usage: uuid:bin([byteorder])', u.bin)
    t.assert_error_msg_equals('Usage: uuid:str()', u.str)
end

g.test_nil_argument = function()
    t.assert_error_msg_matches('builtin/uuid%.lua:%d+: fromstr%(str%)',
                                uuid.fromstr, nil)
end

g.test_gh_5171_is_uuid_function = function()
    t.assert(uuid.is_uuid(uuid.new()))
    t.assert_not(uuid.is_uuid(uuid.new():str()))
    t.assert_not(uuid.is_uuid(1))
    t.assert_not(uuid.is_uuid(require('decimal').new('123')))
end

g.test_compare_uuid_with_string = function()
    local u1str = 'aaaaaaaa-aaaa-4000-b000-000000000001'
    local u2str = 'bbbbbbbb-bbbb-4000-b000-000000000001'
    local u1val = uuid.fromstr(u1str)

    t.assert(u1val == u1str)
    t.assert(u1str == u1val)
    t.assert_not(u1val == u2str)
    t.assert_not(u2str == u1val)

    t.assert_not(u1val > u1str)
    t.assert_not(u1val < u1str)
    t.assert(u1val >= u1str)
    t.assert(u1val <= u1str)

    t.assert_not(u1val > u2str)
    t.assert_not(u1val >= u2str)
    t.assert(u1val < u2str)
    t.assert(u1val <= u2str)
end

g.test_gh_5511_compare_uuids = function()
    local u1 = uuid.fromstr('aaaaaaaa-aaaa-4000-b000-000000000001')
    local u2 = uuid.fromstr('bbbbbbbb-bbbb-4000-b000-000000000001')

    t.assert_not(u1 > u1)
    t.assert(u1 >= u1)
    t.assert(u1 <= u1)
    t.assert_not(u1 < u1)

    t.assert_not(u1 > u2)
    t.assert_not(u1 >= u2)
    t.assert(u1 <= u2)
    t.assert(u1 < u2)
end

local group_invalid_values = t.group('group_invalid_values', {
    {uuid = '', errmsg = 'empty string'},
    {uuid = 'blablabla', errmsg = 'random ASCII string'},
    {uuid = (' '):rep(36), errmsg = 'UUID-length spaces'},
    {uuid = 'ba90d81514e0431d80c0ce587885bb78', errmsg = 'No dash UUID value'},
    {uuid = 'ba90d815-14e0-431d-80c0', errmsg = 'Chopped UUID value'},
    {
        uuid = 'ba90d815-14e0-431d-80c0-tt587885bb7',
        errmsg = 'Non-hex digits in UUID value',
    },
})

group_invalid_values.test = function(cg)
    local u = cg.params.uuid
    local errmsg = cg.params.errmsg
    t.assert_not(uuid.fromstr(u), ('%s conversion failed'):format(errmsg))
end

local TEMPLATE = 'return function() return %s end'

local group_invalid_cmp_1st_arg = t.group('group_invalid_cmp_1st_arg', {
    {cond = "1 < require('uuid').NULL"},
    {cond = "1 <= require('uuid').NULL"},
    {cond = "'abc' < require('uuid').NULL"},
    {cond = "'abc' <= require('uuid').NULL"},
})

group_invalid_cmp_1st_arg.test_gh_5511 = function(cg)
    local fun = load(TEMPLATE:format(cg.params.cond))()
    t.assert_error_msg_contains(
        'incorrect value to convert to uuid as 1 argument', fun
    )
end

local group_invalid_cmp_2nd_arg = t.group('group_invalid_cmp_2nd_arg', {
    {cond = "require('uuid').NULL < 1"},
    {cond = "require('uuid').NULL <= 1"},
    {cond = "require('uuid').NULL < 'abc'"},
    {cond = "require('uuid').NULL <= 'abc'"},
})

group_invalid_cmp_2nd_arg.test_gh_5511 = function(cg)
    local fun = load(TEMPLATE:format(cg.params.cond))()
    t.assert_error_msg_contains(
        'incorrect value to convert to uuid as 2 argument', fun
    )
end
