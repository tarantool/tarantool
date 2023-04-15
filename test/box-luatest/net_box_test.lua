local fiber = require('fiber')
local msgpack = require('msgpack')
local net = require('net.box')
local server = require('luatest.server')
local urilib = require('uri')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.before_test('test_msgpack_object_args', function()
    g.server:eval('function echo(...) return ... end')
    g.server:exec(function()
        box.schema.space.create(
            'T', {format = {{'F1', 'unsigned'}, {'F2', 'string'}}})
        box.space.T:create_index('PK')
    end)
end)

g.after_test('test_msgpack_object_args', function()
    g.server:eval('echo = nil')
    g.server:exec(function()
        box.space.T:drop()
    end)
end)

g.test_msgpack_object_args = function()
    local c = net.connect(g.server.net_box_uri)
    local ret

    -- eval
    t.assert_error_msg_content_equals(
        "Tuple/Key must be MsgPack array",
        function() c:eval('return ...', msgpack.object(123)) end)
    ret = {c:eval('return ...', {msgpack.object(123)})}
    t.assert_equals(ret, {123})
    ret = {c:eval('return ...', msgpack.object({}))}
    t.assert_equals(ret, {})
    ret = {c:eval('return ...', msgpack.object({1, 2, 3}))}
    t.assert_equals(ret, {1, 2, 3})

    -- call
    t.assert_error_msg_content_equals(
        "Tuple/Key must be MsgPack array",
        function() c:call('echo', msgpack.object(123)) end)
    ret = {c:call('echo', {msgpack.object(123)})}
    t.assert_equals(ret, {123})
    ret = {c:call('echo', msgpack.object({}))}
    t.assert_equals(ret, {})
    ret = {c:call('echo', msgpack.object({1, 2, 3}))}
    t.assert_equals(ret, {1, 2, 3})

    -- dml
    ret = c.space.T:insert(msgpack.object({1, 'foo'}))
    t.assert_equals(ret, {1, 'foo'})
    ret = c.space.T:replace({1, 'bar'})
    t.assert_equals(ret, {1, 'bar'})
    ret = c.space.T:update(
        msgpack.object({1}), msgpack.object({{'=', 2, 'baz'}}))
    t.assert_equals(ret, {1, 'baz'})
    ret = c.space.T:select(msgpack.object({1}))
    t.assert_equals(ret, {{1, 'baz'}})
    ret = c.space.T:delete(msgpack.object({1}))
    t.assert_equals(ret, {1, 'baz'})

    -- sql
    c:execute('INSERT INTO T VALUES(?, ?);', msgpack.object({1, 'a'}))
    t.assert_equals(c.space.T:get(1), {1, 'a'})

    c:close()
end

g.before_test('test_msgpack_object_return', function()
    g.server:eval('function echo(...) return ... end')
    g.server:eval([[function echo_mp(...)
        return require('msgpack').object({...})
    end]])
    g.server:exec(function()
        box.schema.space.create(
            'T', {format = {{'F1', 'unsigned'}, {'F2', 'string'}}})
        box.space.T:create_index('PK')
    end)
end)

g.after_test('test_msgpack_object_return', function()
    g.server:eval('echo = nil')
    g.server:eval('echo_mp = nil')
    g.server:exec(function()
        box.space.T:drop()
    end)
end)

g.test_msgpack_object_return = function()
    local c = net.connect(g.server.net_box_uri)
    local opts = {return_raw = true}
    local ret

    -- eval
    ret = c:eval('return ...', {}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {})
    ret = c:eval('return ...', {1, 2, 3}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {1, 2, 3})
    ret = c:eval("return require('msgpack').object({1, 2, 3})", {}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {{1, 2, 3}})
    -- sic: no return_raw
    ret = c:eval("return require('msgpack').object({1, 2, 3})")
    t.assert_not(msgpack.is_object(ret))
    t.assert_equals(ret, {1, 2, 3})

    -- call
    ret = c:call('echo', {}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {})
    ret = c:call('echo', {1, 2, 3}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {1, 2, 3})
    ret = c:call('echo_mp', {1, 2, 3}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {{1, 2, 3}})
    -- sic: no return_raw
    ret = c:call('echo_mp', {1, 2, 3})
    t.assert_not(msgpack.is_object(ret))
    t.assert_equals(ret, {1, 2, 3})

    -- dml
    ret = c.space.T:insert({1, 'foo'}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {1, 'foo'})
    ret = c.space.T:replace({1, 'bar'}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {1, 'bar'})
    ret = c.space.T:update({1}, {{'=', 2, 'baz'}}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {1, 'baz'})
    ret = c.space.T:select({1}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {{1, 'baz'}})
    ret = c.space.T:delete({1}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {1, 'baz'})

    -- min/max/get
    c.space.T:insert({1, 'a'})
    c.space.T:insert({2, 'b'})
    ret = c.space.T.index.PK:min({}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {1, 'a'})
    ret = c.space.T.index.PK:max({}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {2, 'b'})
    ret = c.space.T.index.PK:get({2}, opts)
    t.assert(msgpack.is_object(ret))
    t.assert_equals(ret:decode(), {2, 'b'})

    -- count (ignores return_raw)
    ret = c.space.T.index.PK:count({}, opts)
    t.assert_equals(ret, 2)

    -- upsert (returns nil)
    ret = c.space.T:upsert({1, 'a'}, {{'=', 2, 'a'}}, opts)
    t.assert_equals(ret, nil)

    -- begin/commit/rollback (returns nil)
    local s = c:new_stream()
    ret = s:begin(nil, opts)
    t.assert_equals(ret, nil)
    ret = s:commit(nil, opts)
    t.assert_equals(ret, nil)
    ret = s:rollback(nil, opts)
    t.assert_equals(ret, nil)

    -- sql (ignores return_raw except for rows)
    local metadata = {
        {name = 'F1', type = 'unsigned'},
        {name = 'F2', type = 'string'},
    }
    ret = c:execute('SELECT * FROM T WHERE F1 < ?;', {2}, nil, opts)
    t.assert(msgpack.is_object(ret.rows))
    ret.rows = ret.rows:decode()
    t.assert_equals(ret, {rows = {{1, 'a'}}, metadata = metadata})
    ret = c:prepare('SELECT * FROM T WHERE F1 < ?;', nil, nil, opts)
    local stmt_id = ret.stmt_id
    ret.stmt_id = nil
    t.assert_equals(ret, {metadata = metadata, param_count = 1,
                          params = {{name = "?", type = "ANY"}}})
    ret = c:unprepare(stmt_id, nil, nil, opts)
    t.assert_equals(ret, nil)

    c:close()
end

g.test_connect_uri = function()
    local c
    local opts = {reconnect_after = 42}

    -- URI as string.
    c = net.connect(g.server.net_box_uri)
    t.assert_equals(c.state, 'active')
    c:close()
    c = net:connect(g.server.net_box_uri)
    t.assert_equals(c.state, 'active')
    c:close()
    c = net.connect(g.server.net_box_uri, opts)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c.opts.reconnect_after, 42)
    c:close()
    c = net:connect(g.server.net_box_uri, opts)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c.opts.reconnect_after, 42)
    c:close()

    -- URI as table.
    c = net.connect({g.server.net_box_uri})
    t.assert_equals(c.state, 'active')
    c:close()
    c = net:connect({g.server.net_box_uri})
    t.assert_equals(c.state, 'active')
    c:close()
    c = net.connect({g.server.net_box_uri}, opts)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c.opts.reconnect_after, 42)
    c:close()
    c = net:connect({g.server.net_box_uri}, opts)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c.opts.reconnect_after, 42)
    c:close()

    -- Host and port.
    local uri = urilib.parse(g.server.net_box_uri)
    c = net.connect(uri.host, uri.service)
    t.assert_equals(c.state, 'active')
    c:close()
    c = net:connect(uri.host, uri.service)
    t.assert_equals(c.state, 'active')
    c:close()
    c = net.connect(uri.host, uri.service, opts)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c.opts.reconnect_after, 42)
    c:close()
    c = net:connect(uri.host, uri.service, opts)
    t.assert_equals(c.state, 'active')
    t.assert_equals(c.opts.reconnect_after, 42)
    c:close()

    -- Invalid arguments.
    local errmsg = "usage: connect(uri[, opts] | host, port[, opts])"
    t.assert_error_msg_content_equals(errmsg, net.connect, true)
    t.assert_error_msg_content_equals(errmsg, net.connect, 123, 456)
    t.assert_error_msg_content_equals(errmsg, net.connect, {}, '123')
    t.assert_error_msg_content_equals(errmsg, net.connect, 'localhost', true)
end

g.test_schemaless = function()
    local c
    -- fetch_schema = false
    local schema_update_counter = 0
    c = net.connect(g.server.net_box_uri, {fetch_schema = false})
    t.assert_equals(c.state, 'active')
    t.assert_equals(c.opts.fetch_schema, false)
    t.assert_not_equals(c.space, nil)

    c:on_schema_reload(function()
        schema_update_counter = schema_update_counter + 1
    end)
    c:eval('box.schema.space.create("test_space1")')
    c:eval('box.space.test_space1:drop()')
    t.assert_equals(schema_update_counter, 0)
    c:close()

    -- fetch_schema = true
    c = net.connect(g.server.net_box_uri, {fetch_schema = true})
    t.assert_equals(c.state, 'active')
    t.assert_equals(c.opts.fetch_schema, true)
    t.assert_not_equals(c.space, nil)

    c:on_schema_reload(function()
        schema_update_counter = schema_update_counter + 1
    end)
    c:eval('box.schema.space.create("test_space2")')
    c:eval('box.space.test_space2:drop()')
    t.assert_equals(schema_update_counter, 2)
    c:close()
end

g.after_test('test_schemaless', function()
    g.server:exec(function()
        if box.space.test_space1 ~= nil then
            box.space.test_space1:drop()
        end
        if box.space.test_space2 ~= nil then
            box.space.test_space2:drop()
        end
    end)
end)

g.test_box_error = function()
    t.assert_error_msg_equals(
        "Illegal parameters, Netbox text protocol support was dropped, "..
        "please use require('console').connect() instead",
        net.connect, g.server.net_box_uri, {console = true})
    local c = net.connect(g.server.net_box_uri)

    t.assert_error_msg_equals(
        "Illegal parameters, query id is expected to be numeric",
        c.unprepare, c, "asd")
    c:close()
end

g.test_gc = function()
    local c = net.connect(g.server.net_box_uri)
    t.assert(c:eval('return true'))
    local weak_refs = setmetatable({
        conn = c,
        callback = c._callback,
        transport = c._transport,
    }, {__mode = 'v'})
    t.assert(weak_refs.conn)
    t.assert(weak_refs.callback)
    t.assert(weak_refs.transport)
    c = nil -- luacheck: no unused
    fiber.yield()
    collectgarbage('collect')
    fiber.yield()
    collectgarbage('collect')
    t.assert_not(weak_refs.conn)
    t.assert_not(weak_refs.callback)
    t.assert_not(weak_refs.transport)
end

--
-- Checks that synchronous requests fail with an error in on_connect and
-- on_schema_reload triggers (gh-5358).
--
g.test_sync_request_in_trigger = function()
    local c
    local ch = fiber.channel(100)
    local function trigger_cb()
        local status, err
        status, err = pcall(c.call, c, 'box.session.user', {timeout = 5})
        ch:put(status or tostring(err))
        local fut = c:call('box.session.user', {}, {is_async = true})
        status, err = pcall(fut.wait_result, fut, 5)
        ch:put(status or tostring(err))
        status, err = pcall(function() for _, _ in fut:pairs(5) do end end)
        ch:put(status or tostring(err))
    end
    local function check()
        local msg = 'Synchronous requests are not allowed in net.box trigger'
        t.assert_str_contains(ch:get(5), msg) -- conn:call
        t.assert_str_contains(ch:get(5), msg) -- fut:wait_result
        t.assert_str_contains(ch:get(5), msg) -- fut:pairs
    end
    c = net.connect(g.server.net_box_uri, {wait_connected = false})
    t.assert_equals(c.state, 'initial')
    c:on_connect(trigger_cb)
    c:on_schema_reload(trigger_cb)
    check() -- on_connect
    check() -- on_schema_reload
    c:close()
end
