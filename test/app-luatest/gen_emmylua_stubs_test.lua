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

g.test_private_annotations_are_not_exported = function()
    local dir = fio.tempdir()
    local source = fio.pathjoin(dir, 'module.lua')
    local output = fio.pathjoin(dir, 'module-stub.lua')

    write_file(source, [[---@private
---@class hidden
---@field secret integer

---@class visible
---@field value integer

---@private
---@param x integer
---@return integer
local function hidden_fn(x)
    return x
end

---@param x integer
---@return integer
local function visible_fn(x)
    return x
end
]])

    local ph = assert(popen.new({
        tarantool_binary(), TOOL,
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

    local stub = read_file(output)
    t.assert_not_str_contains(stub, 'hidden')
    t.assert_not_str_contains(stub, 'secret')
    t.assert_not_str_contains(stub, '---@private')
    t.assert_str_contains(stub, '---@class visible')
    t.assert_str_contains(stub, '---@field value integer')
    t.assert_str_contains(stub, 'local function visible_fn(x) end')

    fio.rmtree(dir)
end
