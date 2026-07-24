local t = require('luatest')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group()

local WARN_PATTERN = 'execute permission on the universe'

-- Collect gh-12830 warning messages. Runs on the server.
local function find_warnings(pattern)
    local config = require('config')
    local res = {}
    for _, alert in ipairs(config:info().alerts) do
        if alert.type == 'warn' and alert.message:find(pattern, 1, true) then
            table.insert(res, alert.message)
        end
    end
    return res
end

-- Build a config granting one privilege to each {subject, privilege} pair.
local function privs(...)
    local config = {}
    for _, pair in ipairs({...}) do
        config[pair[1]] = {privileges = {pair[2]}}
    end
    return config
end

-- Shortcut for a single subject.
local function priv(subject, privilege)
    return privs({subject, privilege})
end

-- Verifier asserting the exact number of gh-12830 warnings on the server.
local function expect_warnings(count)
    return {
        verify = function(pattern, find_warnings, count)
            local warns = loadstring(find_warnings)(pattern)
            t.assert_equals(#warns, count,
                ('expected %d warning(s), got: %s'):format(count,
                    require('json').encode(warns)))
            return warns
        end,
        verify_args = {WARN_PATTERN, string.dump(find_warnings), count},
    }
end

-- Run success_case with the given config and warning count expectation.
local function check(config, count)
    local exp = expect_warnings(count)
    helpers.success_case(g, {
        options = config,
        verify = exp.verify,
        verify_args = exp.verify_args,
    })
end

-- Run reload_success_case: `count` warnings for `config`, none for `config2`.
local function check_reload(config, count, config2)
    local exp = expect_warnings(count)
    local none = expect_warnings(0)
    helpers.reload_success_case(g, {
        options = config,
        options_2 = config2,
        verify = exp.verify,
        verify_args = exp.verify_args,
        verify_2 = none.verify,
        verify_args_2 = none.verify_args,
    })
end

-- execute + universe, no lua_call -> one warning.
g.test_universe_execute_warns = function()
    check(priv('credentials.users.alice',
        {permissions = {'execute'}, universe = true}), 1)
end

-- lua_call: [all] silences the warning.
g.test_lua_call_all_no_warning = function()
    check(priv('credentials.users.alice',
        {permissions = {'execute'}, lua_call = {'all'}}), 0)
end

-- A concrete lua_call list does not warn.
g.test_lua_call_named_no_warning = function()
    check(priv('credentials.users.alice',
        {permissions = {'execute'}, lua_call = {'foo'}}), 0)
end

-- Non-execute permissions on the universe do not warn.
g.test_non_execute_universe_no_warning = function()
    check(priv('credentials.users.alice',
        {permissions = {'read', 'write'}, universe = true}), 0)
end

-- The warning fires for roles too.
g.test_role_universe_execute_warns = function()
    check(priv('credentials.roles.myrole',
        {permissions = {'execute'}, universe = true}), 1)
end

-- Narrowing to lua_call on reload drops the warning.
g.test_warning_dropped_on_reload = function()
    check_reload(
        priv('credentials.users.alice',
            {permissions = {'execute'}, universe = true}), 1,
        priv('credentials.users.alice',
            {permissions = {'execute'}, lua_call = {'all'}}))
end

-- Removing the privilege on reload drops the warning.
g.test_warning_dropped_when_privilege_removed = function()
    check_reload(
        priv('credentials.users.alice',
            {permissions = {'execute'}, universe = true}), 1,
        priv('credentials.users.alice',
            {permissions = {'read'}, spaces = {'_priv'}}))
end

-- Each risky subject gets its own distinct alert (per-subject key).
g.test_multiple_subjects_multiple_warnings = function()
    local exp = expect_warnings(2)
    local config = privs(
        {'credentials.users.alice',
            {permissions = {'execute'}, universe = true}},
        {'credentials.roles.myrole',
            {permissions = {'execute'}, universe = true}})
    helpers.success_case(g, {
        options = config,
        verify = function(pattern, find_warnings, count)
            local warns = loadstring(find_warnings)(pattern)
            t.assert_equals(#warns, count)
            -- The two alerts must be distinct.
            t.assert_not_equals(warns[1], warns[2])
        end,
        verify_args = exp.verify_args,
    })
end
