local buffer = require('buffer')
local msgpack = require('msgpack')
local netbox = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix{return_raw = {false, true}})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('i')
        local sf = box.schema.space.create('sf', {format = {'field'}})
        sf:create_index('i')
        rawset(_G, 'f', function() return 1, 2, 3 end)
        rawset(_G, 't', function() return {1, {t = s:get{0}}} end)
        rawset(_G, 'tf', function() return {1, {t = sf:get{0}}} end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

local function res_wrapper(res, unpacked)
    if msgpack.is_object(res) then
        res = res:decode()
        return unpacked and unpack(res) or res
    end
    return res
end

local function test_tuple_method(c, method, tuple, return_raw)
    local res = res_wrapper(c.space.s[method](c.space.s, tuple,
                                              {return_raw = return_raw}))
    t.assert_equals(res:totable(), tuple:totable())
    local resf = res_wrapper(c.space.sf[method](c.space.sf, tuple,
                                                {return_raw = return_raw}))
    t.assert_equals(resf:tomap{names_only = true},
                    tuple:tomap{names_only = true})
end

local function get_call_eval_res(c, method, ret_raw)
    if method == 'call' then
        return res_wrapper(c:call("t", {}, {return_raw = ret_raw}), true)[2].t,
                res_wrapper(c:call("tf", {}, {return_raw = ret_raw}), true)[2].t
    else
        return res_wrapper(c:eval("return t()", {},
                                  {return_raw = ret_raw}), true)[2].t,
               res_wrapper(c:eval("return tf()", {},
                                  {return_raw = ret_raw}), true)[2].t
    end
end

local function test_call_eval_box_tuple_extension(c, method, tuple, ret_raw)
    local res, resf = get_call_eval_res(c, method, ret_raw)
    t.assert(box.tuple.is(res))
    t.assert_equals(res:totable(), tuple:totable())
    t.assert(box.tuple.is(resf))
    t.assert_equals(resf:tomap{names_only = true},
                    tuple:tomap{names_only = true})
end

local function test_call_eval_old(c, method, tuple, ret_raw)
    local res, resf = get_call_eval_res(c, method, ret_raw)
    t.assert_not(box.tuple.is(res))
    t.assert_equals(res, tuple:totable())
    t.assert_not(box.tuple.is(resf))
    t.assert_equals(resf, tuple:totable())
end

local function pack(...)
    return { n = select("#", ...), ... }
end

-- Checks that formats in IPROTO request responses work correctly.
g.test_net_box_formats_in_iproto_request_responses = function(cg)
    local ret_raw = cg.params.return_raw
    local c = netbox:connect(cg.server.net_box_uri, {fetch_schema = false})

    t.assert_equals(c.space.s:get{0}, nil)
    t.assert_equals(c.space.sf:get{0}, nil)
    t.assert_equals(c.space.s:select{}, {})
    t.assert_equals(c.space.sf:select{}, {})
    local tuple_methods = {'insert', 'delete', 'replace', 'get'}
    local fmt = box.tuple.format.new{{'field'}}
    local tuple = box.tuple.new({0}, {format = fmt})
    for _, method in ipairs(tuple_methods) do
        test_tuple_method(c, method, tuple)
    end
    local res = res_wrapper(c.space.s:update({0}, {{'=', 2, 0}},
                                             {return_raw = ret_raw}))
    t.assert_equals(res:totable(), {0, 0})
    c.space.s:replace{0}
    local resf = res_wrapper(c.space.sf:update({0}, {{'=', 2, 0}},
                             {return_raw = ret_raw}))
    t.assert_equals(resf:tomap{names_only = true},
                    tuple:tomap{names_only = true})
    c.space.sf:replace{0}
    res = res_wrapper(c.space.s:select({}, {return_raw = ret_raw}))[1]
    t.assert_equals(res:totable(), tuple:totable())
    resf = res_wrapper(c.space.sf:select({}, {return_raw = ret_raw}))[1]
    t.assert_equals(resf:tomap{names_only = true},
                    tuple:tomap{names_only = true})
    c.space.s:insert{1}
    c.space.sf:insert{1}

    local tuples = {box.tuple.new({0}, {format = fmt}),
                    box.tuple.new({1}, {format = fmt})}
    res = res_wrapper(c.space.s:select({}, {return_raw = ret_raw}))
    resf = res_wrapper(c.space.sf:select({}, {return_raw = ret_raw}))
    for i, tpl in ipairs(tuples) do
        t.assert_equals(res[i]:totable(), tpl:totable())
        t.assert_equals(resf[i]:tomap{names_only = true},
                        tpl:tomap{names_only = true})
    end
    t.assert_equals(pack(c:call("f")), {1, 2, 3, n = 3})
    t.assert_equals(pack(c:eval("return 1, 2, 3")), {1, 2, 3, n = 3})

    test_call_eval_box_tuple_extension(c, "call", tuple, ret_raw)
    test_call_eval_box_tuple_extension(c, "eval", tuple, ret_raw)
end

-- Checks that `box_tuple_extension` backward compatibility option works
-- correctly.
g.test_box_tuple_extension_compat_option = function(cg)
    cg.server:exec(function()
        require('compat').box_tuple_extension = 'old'
    end)

    local c = netbox:connect(cg.server.net_box_uri, {fetch_schema = false})

    t.assert_equals(pack(c:call("f")), {1, 2, 3, n = 3})
    t.assert_equals(pack(c:eval("return 1, 2, 3")), {1, 2, 3, n = 3})

    local tuple = box.tuple.new{0}
    test_call_eval_old(c, "call", tuple)
    test_call_eval_old(c, "eval", tuple)

    local ibuf = buffer.ibuf()
    local data_size = c:call("t", {}, {buffer = ibuf})
    local res = msgpack.object_from_raw(ibuf.rpos, data_size):decode()
    res = res[box.iproto.key.DATA][1][2].t
    t.assert_equals(res, tuple:totable())
end

g.before_test('test_netbox_conn_with_disabled_dml_tuple_extension_errinj',
              function()
    box.error.injection.set('ERRINJ_NETBOX_FLIP_FEATURE',
                            box.iproto.feature.dml_tuple_extension)
end)

-- Checks that net.box connection buffer argument works correctly with formats
-- in IPROTO request responses.
g.test_netbox_conn_with_disabled_dml_tuple_extension_errinj = function(cg)
    t.tarantool.skip_if_not_debug()

    local c = netbox:connect(cg.server.net_box_uri, {fetch_schema = false})
    local res = c.space.sf:get{0}
    t.assert_equals(res:tomap{names_only = true}, {})
end

g.after_test('test_netbox_conn_with_disabled_dml_tuple_extension_errinj',
              function()
    box.error.injection.set('ERRINJ_NETBOX_FLIP_FEATURE', -1)
end)
