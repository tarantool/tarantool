local t = require('luatest')

-- Parse string with version in format 'A.B.C' to version id.
local function parse_version(version)
    local major, minor, patch = version:match('^(%d+)%.(%d+)%.(%d+)$')
    if major == nil then
        error('version should be in format A.B.C')
    end
    return bit.bor(bit.lshift(bit.bor(bit.lshift(major, 8), minor), 8), patch)
end

-- Find oldest application version that can run on given schema version.
-- Return index of this application version in box.schema.downgrade_versions.
local function app_version_index(version_str)
    local version = parse_version(version_str)
    local versions = box.schema.downgrade_versions()
    local idx
    for i, v_str in ipairs(versions) do
        local v = parse_version(v_str)
        if v >= version then
            idx = i
            break
        end
    end
    return idx
end

-- Find oldest application version that can run on given schema version.
local function app_version(version_str)
    local idx = app_version_index(version_str)
    if idx == nil then
        error(string.format("cannot find app version for %s", version_str))
    end
    return box.schema.downgrade_versions()[idx]
end

-- Find newest application version that can NOT run on given schema version
-- (because shcema version is more recent).
local function prev_version(version_str)
    local idx = app_version_index(version_str)
    if idx == nil then
        idx = #box.schema.downgrade_versions()
    else
        idx = idx - 1
    end
    if idx < 1 then
        error(string.format("cannot find previous version for '%s'",
                            version_str))
    end
    return box.schema.downgrade_versions()[idx]
end

-- Check that downgrading to @version blocked by issues listed in @issues.
local function check_issues(version, issues)
    local app_version = app_version(version)
    t.assert_equals(box.schema.downgrade_issues(app_version), {})
    box.schema.downgrade(app_version)

    local prev_version = prev_version(version)
    local more_msg  = " There are more downgrade issues. To list them" ..
                      " all call box.schema.downgrade_issues."
    t.assert_error_msg_contains(issues[1] .. more_msg,
                                box.schema.downgrade, prev_version)
    t.assert_equals(box.schema.downgrade_issues(prev_version), issues)
end

return {
    parse_version = parse_version,
    app_version = app_version,
    prev_version = prev_version,
    check_issues = check_issues,
}
