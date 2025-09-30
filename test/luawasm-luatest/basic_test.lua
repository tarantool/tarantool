local t = require('luatest')
local fio = require('fio')
local treegen = require('luatest.treegen')

local is_wasm = require('tarantool').build.wasm
local g = t.group()

g.before_all(function(g)
    t.skip_if(not is_wasm, 'wasm support is disabled in this build')

    g.dir = treegen.prepare_directory({}, {})
    local wasm_src =
        fio.pathjoin('doc/examples/wasm/hello-component', 'hello.wasm')
    local wasm_dst = fio.pathjoin(g.dir, 'hello.wasm')
    assert(fio.copyfile(wasm_src, wasm_dst))

    g.wasm_path = fio.abspath(wasm_dst)
    g.log_path  = fio.abspath(fio.pathjoin(g.dir, 'hello.log'))
end)

g.after_all(function(g)
    if g.dir and fio.path.is_dir(g.dir) then
        fio.rmtree(g.dir)
    end
end)

g.test_manual_component = function(g)
    local luawasm = require('luawasm')

    local hello = luawasm:new({
        wasm = g.wasm_path,
        config = {
            env = { vars = { GREETING = 'Tarantool' } },
            stdio = { stdout_path = g.log_path },
        },
    })

    t.assert_equals(
        hello:say_hello('Alice'),
        'Hello, Alice! From exported function.'
    )

    hello:run()
    t.assert(hello:join())

    local content = t.helpers.retrying({ timeout = 3 }, function()
        local fh = fio.open(g.log_path, { 'O_RDONLY' })
        if fh == nil then error('no log') end
        local data = fh:read()
        fh:close()
        if data == nil or data == '' then error('empty') end
        return data
    end)

    t.assert_str_contains(content, 'Hello, Tarantool! From run.')
end
