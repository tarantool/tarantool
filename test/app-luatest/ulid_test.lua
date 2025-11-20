local ulid = require('ulid')
local t = require('luatest')

local g = t.group()

local function ts_prefix(s)
    return s:sub(1, 10)
end

g.test_monotonic_and_unique = function()
    local n = 10000
    local prev = ulid.str()
    local seen = { [prev] = true }
    for _ = 2, n do
        local cur = ulid.str()
        assert(prev <= cur, ('ULID order break: %s > %s'):format(prev, cur))
        assert(not seen[cur], ('duplicate ULID at: %s'):format(cur))
        seen[cur] = true
        prev = cur
    end
end

g.test_to_or_from_string = function()
    local u = ulid()
    local ustr = u:str()
    t.assert_equals(#ustr, 26)
    t.assert(ustr:match('^[0-9A-HJKMNP-TV-Z]+$'))
    t.assert_equals(u, ulid.fromstr(ustr))

    local udef = '01ARZ3NDEKTSV4RRFFQ69G5FAR'
    local uval = ulid.fromstr(udef)
    t.assert_equals(uval:str(), udef)
    t.assert_equals(tostring(uval), udef)
    t.assert_equals(uval:str(), tostring(uval))

    t.assert_equals(#ulid.str(), 26)
end

g.test_to_or_from_binary = function()
    local u = ulid()
    t.assert_equals(#u:bin(), 16)

    t.assert_equals(u, ulid.frombin(u:bin()))
    t.assert_equals(#ulid.bin(), 16)
end

g.test_eq_and_nil = function()
    local u = ulid.new()
    local null_str = ('0'):rep(26)
    local null_ulid = ulid.fromstr(null_str)
    t.assert_equals(ulid.NULL, null_ulid)
    t.assert_equals(null_ulid:str(), null_str)
    t.assert_equals(tostring(null_ulid), null_str)
    t.assert(ulid.NULL:isnil())
    t.assert_not_equals(u, ulid.NULL)
    t.assert_not(u:isnil())
    t.assert_not_equals(u, nil)
    t.assert_not_equals(u, 12345)
    t.assert_not_equals(u, 'blablabla')
end

g.test_invalid_usage = function()
    local u = ulid.new()
    t.assert_error_msg_equals('Usage: ulid:isnil()', u.isnil)
    t.assert_error_msg_equals('Usage: ulid:bin()', u.bin)
    t.assert_error_msg_equals('Usage: ulid:str()', u.str)
end

g.test_nil_argument = function()
    t.assert_error_msg_matches('builtin/ulid%.lua:%d+: fromstr%(str%)',
        ulid.fromstr, nil)
end

g.test_is_ulid_function = function()
    t.assert(ulid.is_ulid(ulid.new()))
    t.assert_not(ulid.is_ulid(ulid.new():str()))
    t.assert_not(ulid.is_ulid(1))
    t.assert_not(ulid.is_ulid(require('decimal').new('123')))
end

g.test_compare_ulid_with_string = function()
    local u1str = ulid.str()
    local u2str = ulid.str()
    local u1val = ulid.fromstr(u1str)

    t.assert(u1val == u1str)
    t.assert(u1str == u1val)
    t.assert_not(u1val == u2str)
    t.assert_not(u2str == u1val)

    t.assert_not(u1val > u1str)
    t.assert_not(u1val < u1str)
    t.assert(u1val >= u1str)
    t.assert(u1val <= u1str)

    t.assert(u1val < u2str)
end

g.test_compare_ulids = function()
    local u1 = ulid.fromstr(ulid.str())
    local u2 = ulid.fromstr(ulid.str())

    t.assert_not(u1 > u1)
    t.assert(u1 >= u1)
    t.assert(u1 <= u1)
    t.assert_not(u1 < u1)

    t.assert_not(u1 > u2)
    t.assert_not(u1 >= u2)
    t.assert(u1 <= u2)
    t.assert(u1 < u2)
end

local group_invalid_values = t.group('ulid_invalid_values', {
    { ulid = '', errmsg = 'empty string' },
    { ulid = 'blablabla', errmsg = 'random ASCII string' },
    { ulid = (' '):rep(26), errmsg = 'ULID-length spaces' },
    { ulid = '01ARZ3NDEKTSV4RRFFQ69G5FA', errmsg = 'short ULID' },
    { ulid = '01ARZ3NDEKTSV4RRFFQ69G5FAV0', errmsg = 'long ULID' },
    { ulid = 'UUUUUUUUUUUUUUUUUUUUUUUUUU', errmsg = 'invalid alphabet' },
})

group_invalid_values.test = function(cg)
    local s = cg.params.ulid
    local errmsg = cg.params.errmsg
    t.assert_not(ulid.fromstr(s), ('%s conversion failed'):format(errmsg))
end

local TEMPLATE = 'return function() return %s end'

local group_invalid_cmp_1st_arg = t.group('ulid_invalid_cmp_1st_arg', {
    { cond = "1 < require('ulid').NULL" },
    { cond = "1 <= require('ulid').NULL" },
    { cond = "'abc' < require('ulid').NULL" },
    { cond = "'abc' <= require('ulid').NULL" },
})

group_invalid_cmp_1st_arg.test = function(cg)
    local fun = load(TEMPLATE:format(cg.params.cond))()
    t.assert_error_msg_contains(
        'incorrect value to convert to ulid as 1 argument', fun
    )
end

local group_invalid_cmp_2nd_arg = t.group('ulid_invalid_cmp_2nd_arg', {
    { cond = "require('ulid').NULL < 1" },
    { cond = "require('ulid').NULL <= 1" },
    { cond = "require('ulid').NULL < 'abc'" },
    { cond = "require('ulid').NULL <= 'abc'" },
})

group_invalid_cmp_2nd_arg.test = function(cg)
    local fun = load(TEMPLATE:format(cg.params.cond))()
    t.assert_error_msg_contains(
        'incorrect value to convert to ulid as 2 argument', fun
    )
end

g.test_monotonic_with_frozen_time = function()
    t.tarantool.skip_if_not_debug()
    box.error.injection.set('ERRINJ_ULID_TIME_FREEZE', true)

    local first = ulid.new()
    local prev = first
    local ts = ts_prefix(first:str())

    for i = 1, 10000 do
        local cur = ulid.new()
        local cur_s = cur:str()
        t.assert_equals(ts_prefix(cur_s), ts,
            ('timestamp prefix changed at %d: %s -> %s'):format(
                i, prev:str(), cur_s))
        t.assert(prev < cur,
            ('ULID is not monotonic at %d: %s >= %s'):format(
                i, prev:str(), cur_s))

        prev = cur
    end

    box.error.injection.set('ERRINJ_ULID_TIME_FREEZE', false)
end

g.test_random_overflow_errinj = function()
    t.tarantool.skip_if_not_debug()
    ulid.new()

    box.error.injection.set('ERRINJ_ULID_TIME_FREEZE', true)
    box.error.injection.set('ERRINJ_ULID_RAND_OVERFLOW', true)

    local function check_overflow(gen)
        t.assert_error_msg_contains(
            'ULID random component overflow', gen)
    end

    check_overflow(function() return ulid() end)
    check_overflow(ulid)
    check_overflow(ulid.new)
    check_overflow(ulid.bin)
    check_overflow(ulid.str)

    box.error.injection.set('ERRINJ_ULID_RAND_OVERFLOW', false)
    box.error.injection.set('ERRINJ_ULID_TIME_FREEZE', false)
end
