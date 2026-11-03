local fio = require('fio')
local fun = require('fun')

os.setenv('http_proxy', '')
os.setenv('https_proxy', '')
os.setenv('HTTP_PROXY', '')
os.setenv('HTTPS_PROXY', '')
os.setenv('no_proxy', '*')
os.setenv('NO_PROXY', '*')

local repo_root = fio.cwd()
local http_root = fio.pathjoin(repo_root, 'third_party', 'http')
os.setenv('LUA_SOURCE_DIR', http_root)

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

local function array_to_map(array)
    local result = {}
    for _, value in ipairs(array) do
        result[value] = true
    end
    return result
end

local third_party_tests = get_test_modules_list('third_party/http/test')
local core_tests = get_test_modules_list('test/http-luatest')

core_tests = array_to_map(core_tests)
third_party_tests = array_to_map(third_party_tests)

for third_party_test in pairs(third_party_tests) do
    local name = third_party_test:gsub('^third_party.http.test.', '')
                                 :gsub('%.', '_')
    local core_test = 'test.http-luatest.' .. name
    assert(core_tests[core_test],
           'Expected ' .. core_test .. ' test, but it was not provided')
end

local function preload_from_file(module_name, path, transform)
    local abs_path = fio.pathjoin(repo_root, path)
    package.preload[module_name] = function()
        local module = dofile(abs_path)
        if transform ~= nil then
            module = transform(module)
        end
        return module
    end
end

preload_from_file('test.helpers', 'third_party/http/test/helpers.lua',
                  function(helpers)
    helpers.update_lua_env_variables = function(server)
        server.env.LUA_PATH = http_root .. '/?.lua;' ..
                              repo_root .. '/?.lua;' ..
                              repo_root .. '/?/init.lua;' ..
                              (server.env.LUA_PATH or '')
    end
    return helpers
end)
preload_from_file('test.mocks.mock_role',
                  'third_party/http/test/mocks/mock_role.lua')

for _, path in ipairs(glob_recursive('third_party/http/test')) do
    if path:endswith('_test.lua') then
        local module_name = path:gsub('%.lua$', ''):gsub('/+', '.')
        preload_from_file(module_name, path)
    end
end

return {}
