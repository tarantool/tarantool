local msgpack = require('msgpack')
local netbox = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

local tuple_return_str = [[local t1, t2 = ...
                           return box.tuple.is(t1) and box.tuple.is(t2) and
                                  t1:tomap{names_only = true}.field == 0 and
                                  next(t2:tomap{names_only = true}) == nil ]]

local table_return_str = [[local t1, t2 = ...
                           return type(t1) == 'table' and type(t2) == 'table' ]]
g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function(tuple_return_str, table_return_str)
        local f_tuple = function(t1, t2)
            return box.tuple.is(t1) and box.tuple.is(t2) and
                   t1:tomap{name_only = true}.field == 0 and
                   next(t2:tomap{names_only = true}) == nil
        end
        rawset(_G, 'g_tuple', f_tuple)
        rawset(_G, 'f_tuple', f_tuple)
        box.schema.func.create('f_tuple')
        local tuple_func_str = "function(...) " .. tuple_return_str .. "end"
        box.schema.func.create('p_tuple', {body = tuple_func_str})

        local f_table = function(t1, t2)
            return type(t1) == 'table' and type(t2) == 'table'
        end
        rawset(_G, 'g_table', f_table)
        rawset(_G, 'f_table', f_table)
        box.schema.func.create('f_table')
        local table_func_str = "function(...) " .. table_return_str .. "end"
        box.schema.func.create('p_table', {body = table_func_str})
    end, {tuple_return_str, table_return_str})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks that tuple formats in IPROTO call and eval requests arguments work
-- correctly.
g.test_net_box_formats_in_iproto_call_eval_request_args = function(cg)
    local c = netbox:connect(cg.server.net_box_uri)

    local t1 = box.tuple.new({0}, {format = {{'field'}}})
    local t2 = box.tuple.new{0}
    t.assert(c:call('g_tuple', {t1, t2}))
    t.assert(c:call('f_tuple', {t1, t2}))
    t.assert(c:call('p_tuple', {t1, t2}))
    t.assert(c:eval(tuple_return_str, {t1, t2}))
end

local function inject_call_or_eval(c, request, func_or_expr, args, formats)
    local header = msgpack.encode({
        [box.iproto.key.REQUEST_TYPE] = box.iproto.type[request],
        [box.iproto.key.SYNC] = c:_next_sync(),
    })
    local body = string.fromhex('8' .. (2 + (formats ~= nil and 1 or 0))) ..
        msgpack.encode(request == 'CALL' and box.iproto.key.FUNCTION_NAME or
        request == 'EVAL' and box.iproto.key.EXPR) ..
        msgpack.encode(func_or_expr) ..
        msgpack.encode(box.iproto.key.TUPLE) .. args
    if formats ~= nil then
        body = body .. msgpack.encode(box.iproto.key.TUPLE_FORMATS) .. formats
    end
    local size = msgpack.encode(#header + #body)
    local request = size .. header .. body
    return c:_inject(request)
end

-- Checks that errors with tuple formats in IPROTO call and eval requests
-- arguments are handled correctly.
g.test_net_box_formats_in_iproto_call_eval_request_args_errors = function(cg)
    local c = netbox:connect(cg.server.net_box_uri)

    local raw_data = {
        {"Invalid MsgPack - packet body",
         {string.fromhex('91d607') .. msgpack.encode(box.NULL)}},
        {"Invalid MsgPack - packet body",
         {string.fromhex('91d407cc')}},
        {"Invalid MsgPack - packet body",
         {string.fromhex('91d407') .. msgpack.encode(1)}},
        {"Invalid MsgPack - packet body",
         {string.fromhex('91d507') .. msgpack.encode(1) .. msgpack.encode(1)}},
        {"Invalid MsgPack - packet body",
         {string.fromhex('91d607') .. msgpack.encode(1) ..
          msgpack.encode({1})}},
        {"Invalid MsgPack - packet body",
         {string.fromhex('91d607') .. msgpack.encode(1) ..
          string.fromhex('92') .. msgpack.encode(1)}},
        {"Can not parse a tuple from MsgPack",
         {string.fromhex('91d607') .. msgpack.encode(1) ..
          msgpack.encode({1, 1})}},
        {"Invalid MsgPack - packet body",
         {string.fromhex('91d607') .. msgpack.encode(1) ..
          msgpack.encode({1, 1}), msgpack.encode({[2] = box.NULL})}},
        {"Invalid MsgPack - packet body",
         {string.fromhex('91d607') .. msgpack.encode(1) ..
          msgpack.encode({1, 1}), msgpack.encode{[1] = {box.NULL}}}},
        {"Space field 'field' is duplicate",
         {string.fromhex('91d607') .. msgpack.encode(1) ..
          msgpack.encode({1, 1}), string.fromhex('810192') ..
          msgpack.encode({name = 'field'}) ..
          msgpack.encode({name = 'field'})}},
        {"Can not parse a tuple from MsgPack",
         {string.fromhex('91d607') .. msgpack.encode(1) ..
          msgpack.encode({1, 1}), string.fromhex('810290')}},
    }
    local remote_calls = {
        ['g_tuple'] = 'CALL',
        ['f_tuple'] = 'CALL',
        ['p_tuple'] = 'CALL',
        [tuple_return_str] = 'EVAL',
    }
    for _, raw_datum in ipairs(raw_data) do
        for func_or_expr, request in pairs(remote_calls) do
            t.assert_error_msg_content_equals(raw_datum[1], function()
                inject_call_or_eval(c, request, func_or_expr,
                                    unpack(raw_datum[2]))
            end)
        end
    end
end

-- Checks that `box_tuple_extension` backward compatibility option works
-- correctly.
g.test_box_tuple_extension_compat_option = function(cg)
    cg.server:exec(function()
        require('compat').box_tuple_extension = 'old'
    end)

    local c = netbox:connect(cg.server.net_box_uri)

    local t1 = box.tuple.new({0}, {format = {{'field'}}})
    local t2 = box.tuple.new{0}
    t.assert(c:call('g_table', {t1, t2}))
    t.assert(c:call('f_table', {t1, t2}))
    t.assert(c:call('p_table', {t1, t2}))
    t.assert(c:eval(table_return_str, {t1, t2}))
end
