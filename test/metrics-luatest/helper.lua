local fio = require('fio')
local fun = require('fun')

-- After https://github.com/tarantool/tarantool/issues/7727,
-- external metrics will override built-in one, yet
-- we want to test built-in one here.
-- There is no metrics in default test-run environment,
-- but in case someone has metrics package installed globally
-- and wants to run the tests with pure luatest, we disable
-- override here.
local rock_utils = require('third_party.metrics.test.rock_utils')
rock_utils.remove_override('metrics')
rock_utils.assert_builtin('metrics')

-- There are many tests in metrics. Here we check that no one has been
-- forgotten to include.

-- Borrowed from
-- https://github.com/tarantool/luatest/blob/eef05dd16cc7f62380cedbb9271184223aa395a9/luatest/loader.lua#L6-L38
-- Can't just require luatest internals since the function is not exposed.

-- Returns a list of all nested files within a given path.
-- As the `fio.glob` function does not support `**/*`, this method adds `/*` to
-- the path and globs it until result is empty.
local function glob_recursive(path)
    local pattern = path
    local result = {}
    repeat
        pattern = pattern .. '/*'
        local last_result = fio.glob(pattern)
        for _, item in ipairs(last_result) do
            result[#result + 1] = item
        end
    until #last_result == 0
    return result
end

-- If a directory is given, then it's scanned recursively for the files
-- ending with `_test.lua`.
-- If a `.lua` file is given then it's used as is.
-- The resulting list of files is mapped to Lua's module names.
local function get_test_modules_list(path)
    local files
    if path:endswith('.lua') then
        files = fun.iter({path})
    else
        local list = glob_recursive(path)
        table.sort(list)
        files = fun.iter(list):filter(
            function(x) return x:endswith('_test.lua') end
        )
    end
    return files:
        map(function(x) return x:gsub('%.lua$', '') end):
        map(function(x) return x:gsub('/+', '.') end):
        totable()
end

local function array_to_map(arr)
    local map = {}
    for _, v in ipairs(arr) do
        map[v] = true
    end
    return map
end

local third_party_tests = get_test_modules_list('third_party/metrics/test')
local core_tests = get_test_modules_list('test/metrics-luatest')

-- Embedded metrics does not include cartridge role.
-- We don't check cartridge integration here too, refer to
-- https://github.com/tarantool/cartridge/pull/2047
local ignore_tests = {
    'third_party.metrics.test.integration.cartridge_health_test',
    'third_party.metrics.test.integration.cartridge_hotreload_test',
    'third_party.metrics.test.integration.cartridge_metrics_test',
    'third_party.metrics.test.integration.cartridge_nohttp_test',
    'third_party.metrics.test.integration.cartridge_role_test',
    'third_party.metrics.test.integration.highload_test',
    'third_party.metrics.test.integration.hotreload_test',
    'third_party.metrics.test.unit.cartridge_issues_test',
    'third_party.metrics.test.unit.cartridge_role_test',
}

core_tests = array_to_map(core_tests)
third_party_tests = array_to_map(third_party_tests)
ignore_tests = array_to_map(ignore_tests)

for k, _ in pairs(ignore_tests) do
    third_party_tests[k] = nil
end

for third_party_test, _ in pairs(third_party_tests) do
    -- Replace . to _ since tests here is not hierarchic:
    -- test-run does not expect hierarchic structure.
    local name = third_party_test:gsub('^third_party.metrics.test.', '')
                                 :gsub('%.', '_')
    local core_test = 'test.metrics-luatest.' .. name

    assert(core_tests[core_test],
           "Expected " .. core_test .. " test, but it wasn't provided")
end

-- Workaround paths to reuse existing submodule tests.
local function workaround_requires(path)
    package.preload[path] = function()
        return require('third_party.metrics.' .. path)
    end
end

workaround_requires('test.utils')

require('test.utils')
local test_root = fio.dirname(fio.dirname(fio.abspath(package.search('third_party.metrics.test.utils'))))

package.loaded['test.utils'].LUA_PATH = os.getenv('LUA_PATH') ..
    test_root .. '/?.lua;' ..
    test_root .. '/?/init.lua;'
