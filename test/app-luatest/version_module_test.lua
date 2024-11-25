local t = require('luatest')
local version = require('version')
local ffi = require('ffi')

local g = t.group()

local function assert_version_str_equals(actual, expected)
    -- Remove commit hash, if it exists. It's not saved in version.
    local hash_pos = expected:find('-g')
    if hash_pos then
        expected = expected:sub(1, hash_pos - 1)
    end
    -- Remove trailing dash.
    if expected:sub(-1) == '-' then
        expected = expected:sub(1, -2)
    end
    t.assert_equals(actual, expected)
end

g.test_order = function()
    -- Example of a full version: 2.10.0-beta2-86-gc9981a567.
    local versions = {
        {
            str = '1.2.3-entrypoint',
            ver = version.new(1, 2, 3, 'entrypoint'),
        },
        {
            str = '1.2.3-entrypoint-30',
            ver = version.new(1, 2, 3, 'entrypoint0', 30),
        },
        {
            str = '1.2.3-entrypoint-45',
            ver = version.new(1, 2, 3, 'entrypoint', 45),
        },
        {
            str = '1.2.3-entrypoint1',
            ver = version.new(1, 2, 3, 'entrypoint1', 0),
        },
        {
            str = '1.2.3-entrypoint1-45',
            ver = version.new(1, 2, 3, 'entrypoint1', 45),
        },
        {
            str = '1.2.3-entrypoint2',
            ver = version.new(1, 2, 3, 'entrypoint2'),
        },
        {
            str = '1.2.3-entrypoint2-45',
            ver = version.new(1, 2, 3, 'entrypoint2', 45),
        },
        {
            str = '1.2.3-alpha',
            ver = version.new(1, 2, 3, 'alpha')
        },
        {
            str = '1.2.3-alpha-30',
            ver = version.new(1, 2, 3, 'alpha', 30),
        },
        {
            str = '1.2.3-alpha-45',
            ver = version.new(1, 2, 3, 'alpha', 45),
        },
        {
            str = '1.2.3-alpha1',
            ver = version.new(1, 2, 3, 'alpha1', 0),
        },
        {
            str = '1.2.3-alpha1-45',
            ver = version.new(1, 2, 3, 'alpha1', 45),
        },
        {
            str = '1.2.3-alpha2',
            ver = version.new(1, 2, 3, 'alpha2', 0),
        },
        {
            str = '1.2.3-alpha2-45',
            ver = version.new(1, 2, 3, 'alpha2', 45),
        },
        {
            str = '1.2.3-beta',
            ver = version.new(1, 2, 3, 'beta', 0),
        },
        {
            str = '1.2.3-beta-45',
            ver = version.new(1, 2, 3, 'beta', 45),
        },
        {
            str = '1.2.3-beta1',
            ver = version.new(1, 2, 3, 'beta1', 0),
        },
        {
            str = '1.2.3-beta1-45',
            ver = version.new(1, 2, 3, 'beta1', 45),
        },
        {
            str = '1.2.3-beta2',
            ver = version.new(1, 2, 3, 'beta2'),
        },
        {
            str = '1.2.3-beta2-45',
            ver = version.new(1, 2, 3, 'beta2', 45),
        },
        {
            str = '1.2.3-rc',
            ver = version.new(1, 2, 3, 'rc'),
        },
        {
            str = '1.2.3-rc-45',
            ver = version.new(1, 2, 3, 'rc', 45),
        },
        {
            str = '1.2.3-rc1',
            ver = version.new(1, 2, 3, 'rc1'),
        },
        {
            str = '1.2.3-rc1-45',
            ver = version.new(1, 2, 3, 'rc1', 45),
        },
        {
            str = '1.2.3-rc2',
            ver = version.new(1, 2, 3, 'rc2', 0),
        },
        {
            str = '1.2.3-rc2-45',
            ver = version.new(1, 2, 3, 'rc2', 45),
        },
        {
            str = '1.2.3-rc3',
            ver = version.new(1, 2, 3, 'rc3'),
        },
        {
            str = '1.2.3-rc4',
            ver = version.new(1, 2, 3, 'rc4')
        },
        {
            str = '1.2.3',
            ver = version.new(1, 2, 3)
        },
        {
            str = '1.2.4',
            ver = version.new(1, 2, 4)
        },
        {
            str = '1.2.4-1',
            ver = version.new(1, 2, 4, nil, 1),
        },
        {
            str = '1.2.4-2',
            ver = version.new(1, 2, 4, nil, 2),
        },
        {
            str = '1.2.5-entrypoint',
            ver = version.new(1, 2, 5, 'entrypoint', 0),
        },
        {
            str = '1.2.5-entrypoint1-45-gc9981a567',
            ver = version.new(1, 2, 5, 'entrypoint1', 45),
        },
        {
            str = '1.2.5-alpha',
            ver = version.new(1, 2, 5, 'alpha'),
        },
        {
            str = '1.2.5-alpha1-45-gc9981a567',
            ver = version.new(1, 2, 5, 'alpha1', 45),
        },
        {
            str = '1.2.6-',
            ver = version.new(1, 2, 6),
        },
        {
            str = '1.2.7-entrypoint-',
            ver = version.new(1, 2, 7, 'entrypoint'),
        },
        {
            str = '1.2.7-entrypoint1-',
            ver = version.new(1, 2, 7, 'entrypoint1'),
        },
        {
            str = '1.2.7-entrypoint1-45',
            ver = version.new(1, 2, 7, 'entrypoint1', 45),
        },
        {
            str = '1.2.7-entrypoint1-46-',
            ver = version.new(1, 2, 7, 'entrypoint1', 46),
        },
        {
            str = '1.2.7-alpha-',
            ver = version.new(1, 2, 7, 'alpha')
        },
        {
            str = '1.2.7-alpha1-',
            ver = version.new(1, 2, 7, 'alpha1')
        },
        {
            str = '1.2.7-alpha1-45',
            ver = version.new(1, 2, 7, 'alpha1', 45),
        },
        {
            str = '1.2.7-alpha1-46-',
            ver = version.new(1, 2, 7, 'alpha1', 46),
        },
        {
            str = '1.2.8-entrypoint',
            ver = version.new(1, 2, 8, 'entrypoint'),
        },
        {
            str = '1.2.8-alpha',
            ver = version.new(1, 2, 8, 'alpha'),
        },
        {
            str = '1.2.8-beta',
            ver = version.new(1, 2, 8, 'beta'),
        },
        {
            str = '1.2.8-rc',
            ver = version.new(1, 2, 8, 'rc'),
        },
        {
            str = '1.2.8',
            ver = version.new(1, 2, 8),
        },
    }
    for i, v in pairs(versions) do
        --
        -- Version objects are intentionally compared with relational operators
        -- and not with luatest's assert_le, assert_gt and so on in order to
        -- avoid luatest interfering with sophisticated functions (like it's
        -- done for comparison of tables). Moreover, now luatest doesn't support
        -- assert_gt, assert_ge for non-number types.
        --
        local ver = version.fromstr(v.str)
        assert_version_str_equals(tostring(ver), v.str)

        t.assert(ver == v.ver, ('%d'):format(i))
        t.assert(ver == v.str, ('%d'):format(i))
        t.assert(v.str == ver, ('%d'):format(i))
        t.assert(v.ver == ver, ('%d'):format(i))

        t.assert(ver <= v.ver, ('%d'):format(i))
        t.assert(ver <= v.str, ('%d'):format(i))
        t.assert(v.str <= ver, ('%d'):format(i))
        t.assert(v.ver <= ver, ('%d'):format(i))

        t.assert(ver >= v.ver, ('%d'):format(i))
        t.assert(ver >= v.str, ('%d'):format(i))
        t.assert(v.str >= ver, ('%d'):format(i))
        t.assert(v.ver >= ver, ('%d'):format(i))

        if i > 1 then
            local prev = versions[i - 1]
            t.assert(prev.ver < ver, ('%d'):format(i))
            t.assert(prev.str < ver, ('%d'):format(i))
            t.assert(ver > prev.str, ('%d'):format(i))
            t.assert(ver > prev.ver, ('%d'):format(i))

            t.assert(prev.ver <= ver, ('%d'):format(i))
            t.assert(prev.str <= ver, ('%d'):format(i))
            t.assert(ver >= prev.str, ('%d'):format(i))
            t.assert(ver >= prev.ver, ('%d'):format(i))

            t.assert(ver ~= prev.ver, ('%d'):format(i))
            t.assert(ver ~= prev.str, ('%d'):format(i))
        end
    end
end

g.test_fromstr_error = function()
    local msg = "Error during parsing version string"
    -- Check, that version triplet is mandatory.
    t.assert_error_msg_contains(msg, version.fromstr, 'bad version')
    t.assert_error_msg_contains(msg, version.fromstr, '1.x.x')
    t.assert_error_msg_contains(msg, version, '1.2.x')
    -- Forbid arbitrary symbols between triplet and prerelease.
    t.assert_error_msg_contains(msg, version, '1.2.7Hey-rc4-46-')
    -- Forbid arbitrary symbols between prerelease and commit num.
    t.assert_error_msg_contains(msg, version, '1.2.7-rc4Hey-46-')
    -- Forbid arbitrary symbols not prefixed with symbol '-'.
    t.assert_error_msg_contains(msg, version, '1.2.7.')
    t.assert_error_msg_contains(msg, version, '1.2.7Hey')
    t.assert_error_msg_contains(msg, version, '1.2.7-rc1-20Hey')
    msg = "Unknown prerelease type 'rcHey'"
    t.assert_error_msg_contains(msg, version, '1.2.7-rcHey')
end

g.test_prerelease_argument = function()
    local msg = "Unknown prerelease type '%s'"
    -- Note, that empty prerelease type is also forbidden.
    local cases = { 'rc.5', 'rc5.pre1', '' }
    for _, c in ipairs(cases) do
        t.assert_error_msg_contains(msg:format(c), version.new, 1, 1, 1, c)
    end
end

g.test_arguments_type = function()
    local msg = "should be a number"
    t.assert_error_msg_contains(msg, version.new, 'a', 1, 1)
    t.assert_error_msg_contains(msg, version.new, 1, 'a', 1)
    t.assert_error_msg_contains(msg, version.new, 1, 1, 'a')
    t.assert_error_msg_contains(msg, version.new, 1, 1, 1, 'rc1', 'a')
    msg = "should be a string"
    t.assert_error_msg_contains(msg, version.new, 1, 1, 1, 1)
    t.assert_error_msg_contains(msg, version, 1)
end

g.test_cast_error = function()
    local msg = "Cannot cast to version object"
    t.assert_error_msg_contains(msg, function()
        if version('1.2.3') <= {} then t.assert(true) end
    end)
    t.assert_error_msg_contains(msg, function()
        if version('1.2.3') <= 'a.a.a.' then t.assert(true) end
    end)

    ffi.cdef('struct foo { int bar; };')
    local cdata = ffi.new('struct foo', {bar = 1})
    t.assert_error_msg_contains(msg, function()
        if version('1.2.3') < cdata then t.assert(true) end
    end)
    t.assert_not_equals(version('1.1.1'), cdata)
end

g.test_fields = function()
    local v = version('1.2.5-entrypoint1-45-gc9981a567')
    t.assert_equals(v.major, 1)
    t.assert_equals(v.minor, 2)
    t.assert_equals(v.patch, 5)
    t.assert_equals(v.prerelease , 'entrypoint1')
    t.assert_equals(v.commit , 45)
    t.assert_equals(v.ghash, nil)

    t.assert_equals(type(v.major), 'number')
    t.assert_equals(type(v.prerelease), 'string')

    t.assert_equals(version.new(1, 2, 5, nil).prerelease, nil)
end
