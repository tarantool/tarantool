local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.after_test('test_legacy_opts', function()
    g.server:exec(function()
        box.schema.func.drop('test', {if_exists = true})
    end)
end)

g.test_legacy_opts = function()
    -- New way: no opts sub-table.
    t.assert(g.server:exec(function()
        box.schema.func.create('test', {is_multikey = true})
        local ret = box.func.test.is_multikey
        box.schema.func.drop('test')
        return ret
    end))
    -- Value type is checked.
    t.assert_error_msg_equals(
        "Illegal parameters, options parameter 'is_multikey' should be of " ..
        "type boolean",
        function()
            g.server:exec(function()
                box.schema.func.create('test', {is_multikey = 'test'})
            end)
        end)
    -- Legacy way: with opts sub-table.
    t.assert(g.server:exec(function()
        box.schema.func.create('test', {opts = {is_multikey = true}})
        local ret = box.func.test.is_multikey
        box.schema.func.drop('test')
        return ret
    end))
end

g.before_test('test_msgpack_object_args', function()
    local echo_code = "function(...) return ... end"
    local check_code = "function(o) return require('msgpack').is_object(o) end"
    local decode_code = "function(o) return o:decode() end"
    g.server:eval("echo = " .. echo_code)
    g.server:eval("echo_mp = " .. echo_code)
    g.server:eval("check = " .. check_code)
    g.server:eval("decode = " .. decode_code)
    g.server:exec(function(check_code, decode_code)
        box.schema.func.create('echo')
        box.schema.func.create('echo_mp', {takes_raw_args = true})
        box.schema.func.create('check', {takes_raw_args = true})
        box.schema.func.create('decode', {takes_raw_args = true})
        box.schema.func.create(
            'check_persistent', {body = check_code, takes_raw_args = true})
        box.schema.func.create(
            'decode_persistent', {body = decode_code, takes_raw_args = true})
    end, {check_code, decode_code})
end)

g.after_test('test_msgpack_object_args', function()
    g.server:exec(function()
        box.schema.func.drop('echo')
        box.schema.func.drop('echo_mp')
        box.schema.func.drop('check')
        box.schema.func.drop('decode')
        box.schema.func.drop('check_persistent')
        box.schema.func.drop('decode_persistent')
    end)
    g.server:eval("echo = nil")
    g.server:eval("echo_mp = nil")
    g.server:eval("check = nil")
    g.server:eval("decode = nil")
end)

g.test_msgpack_object_args = function()
    local args = {'foo', 'bar', {foo = 'bar'}}

    -- remote call
    local c = net:connect(g.server.net_box_uri)
    t.assert_equals({c:call('echo', args)}, args)
    t.assert_equals(c:call('echo_mp', args), args)
    t.assert(c:call('check', args))
    t.assert_equals(c:call('decode', args), args)
    t.assert(c:call('check_persistent', args))
    t.assert_equals(c:call('decode_persistent', args), args)
    c:close()

    -- local call
    local call = function(name, args)
        return g.server:exec(function(name, args)
            return box.func[name]:call(args)
        end, {name, args})
    end
    t.assert_equals({call('echo', args)}, args)
    t.assert_equals(call('echo_mp', args), args)
    t.assert(call('check', args))
    t.assert_equals(call('decode', args), args)
    t.assert(call('check_persistent', args))
    t.assert_equals(call('decode_persistent', args), args)

    -- info
    t.assert_not(g.server:eval('return box.func.echo.takes_raw_args'))
    t.assert(g.server:eval('return box.func.echo_mp.takes_raw_args'))
    t.assert(g.server:eval('return box.func.check.takes_raw_args'))
    t.assert(g.server:eval('return box.func.decode.takes_raw_args'))
    t.assert(g.server:eval('return box.func.check_persistent.takes_raw_args'))
    t.assert(g.server:eval('return box.func.decode_persistent.takes_raw_args'))
end

g.test_gh_7822_vfunc_format = function()
    g.server:exec(function()
        t.assert_equals(box.space._vfunc:format(), box.space._func:format())
    end)
end

-- Try to set unknown option using low level API to reach UB issue.
g.test_gh_8463_unknown_option = function()
    g.server:exec(function()
        local t = require('luatest')

        box.schema.func.create('foo_opt', {body = 'function() return end'})
        local f = box.space._func.index.name:get('foo_opt')
        f = f:totable()
        f[16] = {foo = true}
        t.assert_error_msg_equals(
            "Wrong function options: unexpected option 'foo'",
            box.space._func.replace, box.space._func, f)
    end)
end

g.after_test('test_gh_8463_unknown_option', function()
    g.server:exec(function()
        box.schema.func.drop('foo_opt', {if_exists = true})
    end)
end)
