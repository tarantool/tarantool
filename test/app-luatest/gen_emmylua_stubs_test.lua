local fio = require('fio')
local popen = require('popen')
local t = require('luatest')

local g = t.group('gen_emmylua_stubs')

local SRC_DIR = fio.abspath(os.getenv('TARANTOOL_SRC_DIR') or '../..')
local TOOL = fio.pathjoin(SRC_DIR, 'tools/gen-emmylua-stubs.lua')

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

g.test_class_field_docs_stay_with_their_function = function()
    local dir = fio.tempdir()
    local source = fio.pathjoin(dir, 'module.lua')
    local output = fio.pathjoin(dir, 'module-stub.lua')
    local fh = assert(io.open(source, 'w'))
    fh:write([[
---@class mod
---@field first fun(): number
---
---    Description of first.
---@field second fun(): number
---
---    Description of second.

---@type mod
local mod = {}

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
    t.assert_str_contains(text,
        '---    Description of first.\n' ..
        '---@return number\nfunction mod.first() end')
    t.assert_str_contains(text,
        '---    Description of second.\n' ..
        '---@return number\nfunction mod.second() end')

    fio.rmtree(dir)
end

g.test_class_field_nested_and_multiline_signatures = function()
    local dir = fio.tempdir()
    local source = fio.pathjoin(dir, 'module.lua')
    local output = fio.pathjoin(dir, 'module-stub.lua')
    local fh = assert(io.open(source, 'w'))
    fh:write([[
---@class mod
---@field call fun(cb: fun(): number): number
---@field bench fun<T>(
---    func: fun(...: any): T,
---    ...: any
---): { number, T } Measure CPU time.

---@type mod
local mod = {}

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
    t.assert_str_contains(text,
        '---@param cb fun(): number\n' ..
        '---@return number\nfunction mod.call(cb) end')
    t.assert_str_contains(text,
        '---@param func fun(...: any): T\n' ..
        '---@param ... any\n' ..
        '---@return { number, T } Measure CPU time.\n' ..
        'function mod.bench(func, ...) end')

    fio.rmtree(dir)
end

g.test_enum_without_return_name = function()
    local dir = fio.tempdir()
    local source = fio.pathjoin(dir, 'module.lua')
    local output = fio.pathjoin(dir, 'module-stub.lua')
    local fh = assert(io.open(source, 'w'))
    fh:write([[
---@enum Status
local status = {OK = 0}
]])
    fh:close()

    local ph = assert(popen.new({
        arg[-1], TOOL,
        '--out', output,
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
    t.assert_str_contains(text, '---@enum Status')
    t.assert_str_contains(text, 'local status = {}')

    fio.rmtree(dir)
end
