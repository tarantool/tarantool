local fio = require('fio')
local popen = require('popen')
local t = require('luatest')

local g = t.group('gen_emmylua_stubs')

local SRC_DIR = fio.abspath(os.getenv('TARANTOOL_SRC_DIR') or '.')
local TOOL = fio.pathjoin(SRC_DIR, 'tools/gen-emmylua-stubs.lua')
local BUILD_DIR = os.getenv('BUILDDIR')

local function tarantool_binary()
    if BUILD_DIR ~= nil then
        local path = fio.pathjoin(BUILD_DIR, 'src', 'tarantool')
        if fio.path.exists(path) then
            return path
        end
    end
    return arg[-1]
end

local function write_file(path, data)
    local fh = assert(io.open(path, 'w'))
    fh:write(data)
    fh:close()
end

local function read_file(path)
    local fh = assert(io.open(path, 'r'))
    local data = fh:read('*a')
    fh:close()
    return data
end

local function read_all(ph)
    local res = ''
    while true do
        local chunk = ph:read()
        if chunk == nil or chunk == '' then
            return res
        end
        res = res .. chunk
    end
end

local function generate(dir, source, return_name)
    local output = fio.pathjoin(dir, 'module-stub.lua')
    local args = {tarantool_binary(), TOOL, '--out', output}
    if return_name ~= nil then
        table.insert(args, '--return')
        table.insert(args, return_name)
    end
    table.insert(args, source)
    local ph = assert(popen.new(args, {
        stdout = popen.opts.PIPE,
        stderr = popen.opts.DEVNULL,
    }))
    local out = read_all(ph)
    local st = ph:wait()
    ph:close()
    t.assert_equals(st.exit_code, 0, out)
    return read_file(output)
end

g.test_module_class_functions = function()
    local dir = fio.tempdir()
    local source = fio.pathjoin(dir, 'module.lua')

    write_file(source, [[
---The `mod` module.
---
---@class mod
---@field new fun(parts: table): mod_object Create a new object.
---
---**Example:**
---
---```lua
---mod.new({})
---```
---@field compare fun(a: table, b: table): integer Compare
---    two values.
---@field merge fun(first: table,
---    second: table): integer Merge
---    two tables.
---@field normalize fun(value?: string, ...: any): string Normalize a value.
---@type mod
local mod = require('mod')

---@class mod_object
---@field value integer The value.

return mod
]])

    local stub = generate(dir, source, 'mod')
    t.assert(loadstring(stub) ~= nil)
    t.assert_str_contains(stub, 'function mod.new(parts) end')
    t.assert_str_contains(stub, 'function mod.compare(a, b) end')
    t.assert_str_contains(stub,
        '---@param parts table\n---@return mod_object')
    t.assert_str_contains(stub,
        '---@param a table\n---@param b table\n---@return integer')
    t.assert_str_contains(stub, '---Create a new object.')
    t.assert_str_contains(stub, '---**Example:**')
    t.assert_str_contains(stub, '---```lua')
    t.assert_str_contains(stub, '---mod.new({})')
    t.assert_str_contains(stub, '---Compare')
    t.assert_str_contains(stub, '---two values.')
    t.assert_str_contains(stub, 'function mod.merge(first, second) end')
    t.assert_str_contains(stub,
        '---@param first table\n---@param second table\n---@return integer')
    t.assert_str_contains(stub, '---Merge two tables.')
    t.assert_str_contains(stub, 'function mod.normalize(value, ...) end')
    t.assert_str_contains(stub,
        '---@param value? string\n---@param ... any\n---@return string')
    t.assert_str_contains(stub, '---@class mod')
    t.assert_str_contains(stub, '---@field new fun(parts: table)')
    t.assert_str_contains(stub, '---@class mod_object')
    t.assert_not_str_contains(stub, '---@type mod')

    local _, module_count = stub:gsub('The `mod` module%.', '')
    t.assert_equals(module_count, 1)
    local _, mod_count = stub:gsub('local mod = {}', '')
    t.assert_equals(mod_count, 1)

    fio.rmtree(dir)
end

g.test_without_return_name = function()
    local dir = fio.tempdir()
    local source = fio.pathjoin(dir, 'module.lua')

    write_file(source, [[
---A value.
---@type integer
local value = 1
]])

    local stub = generate(dir, source)
    t.assert_str_contains(stub, '---@type integer')

    fio.rmtree(dir)
end

g.test_annotated_field_assignment = function()
    local dir = fio.tempdir()
    local source = fio.pathjoin(dir, 'module.lua')
    local output = fio.pathjoin(dir, 'module-stub.lua')
    local fh = assert(io.open(source, 'w'))
    fh:write([[
local mod = {}

---A nil value.
---@type integer
mod.NULL = 0

---Create a value.
---@return integer
function mod.new()
    return 1
end

return mod
]])
    fh:close()

    local ph = assert(popen.new({
        arg[-1], TOOL,
        '--out', output,
        '--return', 'mod',
        source,
    }, {
        stdout = popen.opts.PIPE,
        stderr = popen.opts.DEVNULL,
    }))
    local out = read_all(ph)
    local st = ph:wait()
    ph:close()
    t.assert_equals(st.exit_code, 0, out)

    local stub = assert(io.open(output, 'r'))
    local text = stub:read('*a')
    stub:close()
    t.assert_str_contains(text, '---@type integer\nmod.NULL = nil')

    fio.rmtree(dir)
end

g.test_only_annotated_field = function()
    local dir = fio.tempdir()
    local source = fio.pathjoin(dir, 'module.lua')
    local output = fio.pathjoin(dir, 'module-stub.lua')
    local fh = assert(io.open(source, 'w'))
    fh:write([[
local mod = {}

---A value.
---@type integer
mod.VALUE = 42

return mod
]])
    fh:close()

    local ph = assert(popen.new({
        arg[-1], TOOL,
        '--out', output,
        '--return', 'mod',
        source,
    }, {
        stdout = popen.opts.PIPE,
        stderr = popen.opts.DEVNULL,
    }))
    local out = read_all(ph)
    local st = ph:wait()
    ph:close()
    t.assert_equals(st.exit_code, 0, out)

    local stub = assert(io.open(output, 'r'))
    local text = stub:read('*a')
    stub:close()
    t.assert_str_contains(text, 'local mod = {}')
    t.assert_str_contains(text, '---@type integer\nmod.VALUE = nil')
    t.assert_str_contains(text, 'return mod')

    fio.rmtree(dir)
end

g.test_module_table_with_comments = function()
    local dir = fio.tempdir()
    local source = fio.pathjoin(dir, 'module.lua')
    local output = fio.pathjoin(dir, 'module-stub.lua')
    local fh = assert(io.open(source, 'w'))
    fh:write([[
---@type table
local alpha = {}

---@type table
local beta = {}

---Module value.
local mod = {
    ---First field.
    alpha = alpha,
    -- An ordinary comment.
    beta = beta,
}

return mod
]])
    fh:close()

    local ph = assert(popen.new({
        arg[-1], TOOL,
        '--out', output,
        '--return', 'mod',
        source,
    }, {
        stdout = popen.opts.PIPE,
        stderr = popen.opts.DEVNULL,
    }))
    local out = read_all(ph)
    local st = ph:wait()
    ph:close()
    t.assert_equals(st.exit_code, 0, out)

    local stub = assert(io.open(output, 'r'))
    local text = stub:read('*a')
    stub:close()
    t.assert_str_contains(text, '---First field.')
    t.assert_str_contains(text, '-- An ordinary comment.')
    t.assert_str_contains(text, 'alpha = alpha,')
    t.assert_str_contains(text, 'beta = beta,')
    t.assert(not text:find('local mod = {}', 1, true),
        'module table fell back to an empty table:\n' .. text)

    fio.rmtree(dir)
end
