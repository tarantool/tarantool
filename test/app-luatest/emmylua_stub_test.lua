local fio = require('fio')
local justrun = require('luatest.justrun')
local t = require('luatest')
local treegen = require('luatest.treegen')
local emmylua = require('test.app-luatest.emmylua')

local g = t.group('emmylua_stub')

local root = emmylua.source_root()
local script = fio.pathjoin(root, 'tools', 'gen-emmylua-stubs.lua')

-- Count non-overlapping occurrences of a plain substring.
local function count(text, needle)
    local n = 0
    local pos = 1
    while true do
        local _, last = text:find(needle, pos, true)
        if last == nil then
            return n
        end
        n = n + 1
        pos = last + 1
    end
end

-- Run the stub generator over the given files and return the generated
-- stub and the generator's stderr.
local function generate(dir, files, return_name)
    local out = fio.pathjoin(dir, 'stub.lua')
    local args = {script, '--out', out}
    if return_name ~= nil then
        table.insert(args, '--return')
        table.insert(args, return_name)
    end
    for _, file in ipairs(files) do
        table.insert(args, file)
    end
    local res = justrun.tarantool(dir, {}, args, {
        nojson = true,
        stderr = true,
        quote_args = true,
    })
    t.assert_equals(res.exit_code, 0, res.stderr)
    return emmylua.read_file(out), res.stderr
end

g.test_export_assignments = function()
    local dir = treegen.prepare_directory({})
    local module = treegen.write_file(dir, 'module.lua', [[
--- Module.
local exports = {}

---@param a number
local foo = function(a)
    return a
end

---@param x number
---@return number
exports.foo = export1(foo)

--- An alias for foo.
exports.alias = exports.foo

return exports
]])
    local stub = generate(dir, {module}, 'exports')
    t.assert(loadstring(stub) ~= nil)
    t.assert_equals(count(stub, 'local function foo(a) end'), 1)
    t.assert_equals(count(stub, 'exports.foo = foo'), 1)
    t.assert_equals(count(stub, 'exports.alias = exports.foo'), 1)
    -- The block above the assignment must not be emitted twice.
    t.assert_equals(count(stub, '---@param x number'), 1)
    t.assert_equals(count(stub, '--- An alias for foo.'), 1)
end

g.test_function_export_alias = function()
    local dir = treegen.prepare_directory({})
    local module = treegen.write_file(dir, 'module.lua', [[
--- Module.
local exports = {}

---@param x number
exports.fn = function(x)
    return x
end

exports.alias = exports.fn

return exports
]])
    local stub = generate(dir, {module}, 'exports')
    t.assert_equals(count(stub, 'function exports.fn(x) end'), 1)
    t.assert_equals(count(stub, 'exports.alias = exports.fn'), 1)
end

g.test_unresolved_export = function()
    local dir = treegen.prepare_directory({})
    local module = treegen.write_file(dir, 'module.lua', [[
--- Module.
local exports = {}

exports.missing = missing

return exports
]])
    local stub, stderr = generate(dir, {module}, 'exports')
    t.assert_str_contains(stub, 'return exports')
    t.assert_str_contains(stderr,
        "cannot resolve 'exports.missing = missing'")
end

g.test_fun_module = function()
    local dir = treegen.prepare_directory({})
    local source = fio.pathjoin(root, 'third_party', 'luafun', 'fun.lua')
    local stub = generate(dir, {source}, 'exports')
    t.assert(loadstring(stub) ~= nil)
    for _, line in ipairs({
        'exports.each = each',
        'exports.map = map',
        'exports.filter = filter',
        'exports.for_each = exports.each',
        'return exports',
    }) do
        t.assert_equals(count(stub, line), 1)
    end
end
