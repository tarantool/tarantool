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

g.test_module_table_with_closing_brace_in_comment = function()
    local dir = fio.tempdir()
    local source = fio.pathjoin(dir, 'module.lua')
    local output = fio.pathjoin(dir, 'module-stub.lua')
    local fh = assert(io.open(source, 'w'))
    fh:write([[
---@type table
local alpha = {}

---@type table
local beta = {}

---Example module.
local mod = {
    ---First field.
    alpha = alpha,
    ---A literal closing brace: }
    -- Another literal closing brace: }
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
    t.assert_str_contains(text, '---A literal closing brace: }')
    t.assert_str_contains(text, '-- Another literal closing brace: }')
    t.assert_str_contains(text, 'alpha = alpha,')
    t.assert_str_contains(text, 'beta = beta,')
    t.assert(not text:find('local mod = {}', 1, true),
        'module table fell back to an empty table:\n' .. text)

    fio.rmtree(dir)
end
