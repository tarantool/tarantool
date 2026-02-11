local fio = require('fio')
local fun = require('fun')

-- Ensure local HTTP requests in tests are not routed via proxy from
-- environment variables set in CI / development containers.
os.setenv('http_proxy', '')
os.setenv('https_proxy', '')
os.setenv('HTTP_PROXY', '')
os.setenv('HTTPS_PROXY', '')
os.setenv('no_proxy', '*')
os.setenv('NO_PROXY', '*')

-- There are many tests in metrics-export-role. Here we check
-- that no one has been forgotten to include.

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

local third_party_tests =
    get_test_modules_list('third_party/metrics-export-role/test')
local core_tests = get_test_modules_list('test/metrics-export-role-luatest')

core_tests = array_to_map(core_tests)
third_party_tests = array_to_map(third_party_tests)

for third_party_test, _ in pairs(third_party_tests) do
    -- Replace . to _ since tests here is not hierarchic:
    -- test-run does not expect hierarchic structure.
    local name =
        third_party_test:gsub('^third_party.metrics%-export%-role.test.', '')
                        :gsub('%.', '_')
    local core_test = 'test.metrics-export-role-luatest.' .. name

    assert(core_tests[core_test],
           'Expected ' .. core_test .. ' test, but it was not provided')
end

local repo_root = fio.cwd()

local function preload_from_file(module_name, path)
    local abs_path = fio.pathjoin(repo_root, path)
    package.preload[module_name] = function()
        return dofile(abs_path)
    end
end

-- Workaround paths to reuse existing submodule tests.
preload_from_file('test.helpers',
                  'third_party/metrics-export-role/test/helpers/init.lua')
preload_from_file('test.helpers.server',
                  'third_party/metrics-export-role/test/helpers/server.lua')
preload_from_file('test.helpers.mocks',
                  'third_party/metrics-export-role/test/helpers/mocks.lua')


local function preload_upstream_test_module(path)
    local module_name = path:gsub('%.lua$', ''):gsub('/+', '.')
    local abs_path = fio.pathjoin(repo_root, path)
    package.preload[module_name] = function()
        return dofile(abs_path)
    end
end

for _, p in ipairs(glob_recursive('third_party/metrics-export-role/test')) do
    if p:endswith('_test.lua') then
        preload_upstream_test_module(p)
    end
end

local test_root = 'third_party/metrics-export-role/test'
local orig_pathjoin = fio.pathjoin
fio.pathjoin = function(...)
    local args = {...}
    if args[1] == 'test' then
        args[1] = test_root
    end
    return orig_pathjoin(unpack(args))
end

local orig_abspath = fio.abspath
fio.abspath = function(path)
    if type(path) == 'string' and path:startswith('test/') then
        path = path:gsub('^test/', test_root .. '/')
    end
    return orig_abspath(path)
end

local has_http_server, _ = pcall(require, 'http.server')

if not has_http_server then
    return function()
        local t = require('luatest')
        t.skip('http.server is not available')
    end
else
    return function() end
end
