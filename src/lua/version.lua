--
-- Version parser adopted to the Tarantool's versioning policy:
-- https://www.tarantool.io/en/doc/latest/release/policy/#versioning-policy
--
-- Tarantool's versions use SemVer-like format:
--     x.y.z-typeN-commit-ghash
--
-- * x.y.z - major, minor, patch release numbers;
-- * typeN - pre-release name and its optional number: alpha1, rc10. Optional;
-- * commit - number of commits since the latest release. Optional;
-- * ghash - latest commit hash in format g<hash>. Optional.
--
-- Differences with the semver docs:
--
-- * Pre-release identifiers are not separated. Not 'alpha.1' but 'alpha1'.
-- * Pre-release identifiers cannot be nested. Not x.y.z-alpha.beta.
-- * Pre-release identifier always includes non-numeric type: e.g. beta, rc.
-- * Build metadata is denoted by appending hyphen, not plus sign. Not
--   x.y.z-typeN+commit-ghash, but x.y.z-typeN-commit-ghash.
--

local ffi   = require('ffi')
local utils = require('internal.utils')

local check_param = utils.check_param

--
-- Cdata is used in order to allow comparison of the version object with
-- strings. Since relational metamethods do not support mixed types in Lua,
-- using tables is not possible.
--
ffi.cdef([[
    struct version {
        /** Major number. */
        uint32_t major;
        /** Minor number. */
        uint32_t minor;
        /** Patch number. */
        uint32_t patch;
        /** Pre-release name id. */
        uint8_t _prerelease_id;
        /** Pre-release number: e.g. in alpha1 it's 1. */
        uint16_t _prerelease_num;
        /** Number of commits since release. */
        uint32_t commit;
    };
]])

--
-- Pre-release names sorted in descending order.
-- ID 0 is reserved for missing pre-release name.
--
local prerelease_names = {
    'rc',
    'beta',
    'alpha',
    'entrypoint',
}

local prerelease_name_to_id = {}
for id, name in ipairs(prerelease_names) do
    prerelease_name_to_id[name] = id
end

local function version_new(major, minor, patch, prerelease, commit)
    commit = commit or 0
    check_param(major, 'major', 'number', 2)
    check_param(minor, 'minor', 'number', 2)
    check_param(patch, 'patch', 'number', 2)
    check_param(commit, 'commit', 'number', 2)

    local prerelease_id
    local prerelease_num = 0
    if prerelease then
        local prerelease_name
        check_param(prerelease, 'prerelease', 'string', 2)
        prerelease_name, prerelease_num = prerelease:match('^(%a+)(%d+)$')
        if not prerelease_name then
            prerelease_name = prerelease:match('^(%a+)$')
        else
            prerelease_num = tonumber(prerelease_num)
        end
        prerelease_id = prerelease_name_to_id[prerelease_name]
        if not prerelease_id then
            local msg = "Unknown prerelease type '%s'"
            box.error(box.error.ILLEGAL_PARAMS,
                      string.format(msg, prerelease_name or prerelease), 2)
        end
    else
        -- Greatest priority, actual release.
        prerelease_id = 0
    end

    return ffi.new('struct version', {
        major = major,
        minor = minor,
        patch = patch,
        _prerelease_id = prerelease_id,
        _prerelease_num = prerelease_num,
        commit = commit,
    })
end

local function version_fromstr(version_str)
    check_param(version_str, 'version_str', 'string', 2)
    local str = version_str
    --  x.x.x-name<num>-<num>-g<commit>
    -- \____/\________/\_____/
    --   P1      P2      P3
    local major, minor, patch, prerelease, commit, pos, end_pos, tmp_pos, _

    -- Part 1 - version ID triplet. Compulsory fields.
    _, end_pos, major, minor, patch = str:find('^(%d+)%.(%d+)%.(%d+)')
    if not major or not minor or not patch then
        goto error
    end
    major = tonumber(major)
    minor = tonumber(minor)
    patch = tonumber(patch)
    -- Cut to 'type<num>-<num>-g<commit>'.
    pos = str:find('-')
    if not pos then
        goto finish
    end
    if pos ~= end_pos + 1 then
        -- Do not allow arbitrary symbols between version triplet and
        -- prerelease: e.g. forbid 3.3.0Hello-rc1.
        goto error
    end
    str = str:sub(pos + 1)

    -- Part 2 - prerelease name and number, might be absent.
    _, tmp_pos, prerelease = str:find('^(%a+%d*)')
    -- Cut to '<num>-g<commit>'.
    if prerelease then
        end_pos = tmp_pos
        pos = str:find('-')
        if not pos then
            goto finish
        end
        if pos ~= end_pos + 1 then
            -- Do not allow arbitrary symbols between prerelease and commit:
            -- e.g. forbid 3.3.0-rc1.Hello-20
            goto error
        end
        str = str:sub(pos + 1)
    end

    -- Part 3 - commit count since the latest release, might be absent.
    _, tmp_pos, commit = str:find('^(%d+)')
    if commit then
        end_pos = tmp_pos
        commit = tonumber(commit)
    end

::finish::
    -- Note, that ghash itself and everything after it (e.g. -dev suffix) is
    -- ignored and doesn't affect comparing of the versions. Only
    -- x.y.z-typeN-commit part is parsed and validated. However, if anything
    -- is present after version string it must be prefixed with '-' symbol.
    str = str:sub(end_pos + 1)
    if str ~= '' and not str:match('^(-.*)$') then
        goto error
    end
    do return version_new(major, minor, patch, prerelease, commit) end

::error::
    local msg = "Error during parsing version string '%s'"
    box.error(box.error.ILLEGAL_PARAMS, string.format(msg, version_str), 2)
end

local function version_cmp(ver1, ver2)
    if ver1.major ~= ver2.major then
        return ver1.major - ver2.major
    end
    if ver1.minor ~= ver2.minor then
        return ver1.minor - ver2.minor
    end
    if ver1.patch ~= ver2.patch then
        return ver1.patch - ver2.patch
    end
    if ver1._prerelease_id ~= ver2._prerelease_id then
        -- Intentionally inverted, since in descending order.
        return ver2._prerelease_id - ver1._prerelease_id
    end
    if ver1._prerelease_num ~= ver2._prerelease_num then
        return ver1._prerelease_num - ver2._prerelease_num
    end
    if ver1.commit ~= ver2.commit then
        return ver1.commit - ver2.commit
    end
    return 0
end

local version_ctype
local function version_cast(version)
    if type(version) == 'cdata' then
        if ffi.typeof(version) ~= version_ctype then
            return nil
        end
        -- Nothing to do.
        return version
    end
    if type(version) == 'string' then
        -- Convert to version object.
        local ok, v = pcall(version_fromstr, version)
        return ok and v
    end
    return nil
end

local function version_get_prerelease(v)
    if v._prerelease_id == 0 then
        return nil
    end
    local name = prerelease_names[v._prerelease_id]
    if v._prerelease_num ~= 0 then
        name = name .. v._prerelease_num
    end
    return name
end

local version_mt = {
    __index = function(v, field)
        if field == 'prerelease' then
            return version_get_prerelease(v)
        end
    end,
    __eq = function(l, r)
        l = version_cast(l)
        r = version_cast(r)
        if not l or not r then
            return false
        end
        return version_cmp(l, r) == 0
    end,
    __lt = function(l, r)
        l = version_cast(l)
        r = version_cast(r)
        if not l or not r then
            box.error(box.error.ILLEGAL_PARAMS,
                      "Cannot cast to version object", 2)
        end
        return version_cmp(l, r) < 0
    end,
    __le = function(l, r)
        l = version_cast(l)
        r = version_cast(r)
        if not l or not r then
            box.error(box.error.ILLEGAL_PARAMS,
                      "Cannot cast to version object", 2)
        end
        return version_cmp(l, r) <= 0
    end,
    __tostring = function(v)
        local str = v.major .. '.' .. v.minor .. '.' .. v.patch
        local prerelease = version_get_prerelease(v)
        if prerelease then
            str = str .. '-' .. prerelease
        end
        if v.commit ~= 0 then
            str = str .. '-' .. v.commit
        end
        return str
    end,
}

version_ctype = ffi.metatype('struct version', version_mt)

return setmetatable({
    new = version_new,
    fromstr = version_fromstr,
}, {
    __call = function(self, ...) return version_fromstr(...) end,
})
