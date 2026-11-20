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

g.test_annotated_field_assignment = function()
    local dir = fio.tempdir()
    local source = fio.pathjoin(dir, 'module.lua')
    local output = fio.pathjoin(dir, 'module-stub.lua')

    write_file(source, [[
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

    local ph = assert(popen.new({
        tarantool_binary(), TOOL,
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

    local stub = read_file(output)
    t.assert_str_contains(stub, '---@type integer\nmod.NULL = nil')

    fio.rmtree(dir)
end
