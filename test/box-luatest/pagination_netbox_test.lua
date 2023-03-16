local server = require('luatest.server')
local t = require('luatest')
local net = require('net.box')

local net_g = t.group('Pagination in net.box tests')

net_g.before_all(function(cg)
    cg.server = server:new{
        alias   = 'default',
    }
    cg.server:start()
end)

net_g.after_all(function(cg)
    cg.server:drop()
end)

net_g.before_each(function(cg)
    cg.server:exec(function()
        box.schema.space.create('s')
    end)
end)

net_g.after_each(function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

net_g.test_net_box_simple = function(cg)
    cg.server:exec(function()
        local s = box.space.s
        s:create_index("pk", {type = "tree"})
        s:create_index("sk", {parts = {2}, type = "tree", unique=false})
    end)
    local net_box = require('net.box')
    local conn = net_box.connect(cg.server.net_box_uri)
    local s = conn.space.s

    -- Fetch position in empty space
    local tuples, pos = s:select(nil, {limit=2, fetch_pos=true})
    t.assert_equals(tuples, {})
    t.assert_equals(pos, nil)

    cg.server:exec(function()
        for i = 1, 11 do
            box.space.s:replace{i, 1}
        end
    end)
    local tuples1
    local tuples2
    local tuples_offset
    local pos = ""
    local last_tuple = box.NULL

    -- Test fullscan pagination
    for i = 0, 5 do
        tuples1, pos = s:select(nil, {limit=2, fetch_pos=true, after=pos})
        tuples2 = s:select(nil, {limit=2, after=last_tuple})
        last_tuple = tuples2[#tuples2]
        tuples_offset = s:select(nil, {limit=2, offset=i*2})
        t.assert_equals(tuples1, tuples_offset)
        t.assert_equals(tuples2, tuples_offset)
    end
    tuples1, pos = s:select(nil, {limit=2, fetch_pos=true, after=pos})
    t.assert_equals(tuples1, {})
    t.assert_equals(pos, nil)

    -- Test pagination on range iterators
    local key_iter = {
        ['GE'] = 3,
        ['GT'] = 2,
        ['LE'] = 9,
        ['LT'] = 10,
    }
    for iter, key in pairs(key_iter) do
        pos = ""
        last_tuple = box.NULL
        for i = 0, 4 do
            tuples1, pos = s:select(key,
                    {limit=2, iterator=iter, fetch_pos=true, after=pos})
            tuples2 = s:select(key,
                    {limit=2, iterator=iter, after=last_tuple})
            last_tuple = tuples2[#tuples2]
            tuples_offset = s:select(key,
                    {limit=2, iterator=iter, offset=i*2})
            t.assert_equals(tuples1, tuples_offset)
            t.assert_equals(tuples2, tuples_offset)
        end
        tuples1, pos = s:select(key,
                    {limit=2, iterator=iter, fetch_pos=true, after=pos})
            t.assert_equals(tuples1, {})
            t.assert_equals(pos, nil)
    end
end

net_g.test_net_box_async = function(cg)
    cg.server:exec(function()
        local s = box.space.s
        s:create_index("pk", {type = "tree"})
        s:create_index("sk", {parts = {2}, type = "tree", unique=false})
    end)
    local net_box = require('net.box')
    local conn = net_box.connect(cg.server.net_box_uri)
    local s = conn.space.s
    local future
    local TIMEOUT = 10

    -- Fetch position in empty space
    future = s:select(nil, {limit=2, fetch_pos=true, is_async=true})
    local res = future:wait_result(TIMEOUT)
    t.assert_equals(#res, 1)
    t.assert_equals(res[1], {})

    cg.server:exec(function()
        for i = 1, 11 do
            box.space.s:replace{i, 1}
        end
    end)
    local tuples1
    local tuples2
    local tuples_offset
    local pos = ""
    local last_tuple = box.NULL

    -- Test fullscan pagination
    for i = 0, 5 do
        future = s:select(nil, {limit=2, fetch_pos=true, after=pos, is_async=true})
        res = future:wait_result(TIMEOUT)
        tuples1 = res[1]
        pos = res[2]
        -- Check if future:pairs() method works correctly with pagination.
        for msg_no, msg in future:pairs() do
            t.assert_equals(msg_no, 1)
            t.assert_equals(msg[1], tuples1)
            t.assert_equals(msg[2], pos)
        end
        future = s:select(nil, {limit=2, after=last_tuple, is_async=true})
        tuples2 = future:wait_result(TIMEOUT)
        last_tuple = tuples2[#tuples2]
        tuples_offset = s:select(nil, {limit=2, offset=i*2})
        t.assert_equals(tuples1, tuples_offset)
        t.assert_equals(tuples2, tuples_offset)
    end
    future = s:select(nil, {limit=2, fetch_pos=true, after=pos, is_async=true})
    res = future:wait_result(TIMEOUT)
    t.assert_equals(res[1], {})
    t.assert_equals(#res, 1)
end

net_g.test_net_box_raw = function(cg)
    cg.server:exec(function()
        local s = box.space.s
        s:create_index("pk", {type = "tree"})
        s:create_index("sk", {parts = {2}, type = "tree", unique=false})
    end)
    local net_box = require('net.box')
    local conn = net_box.connect(cg.server.net_box_uri)
    local s = conn.space.s

    -- Fetch position in empty space
    local tuples, pos = s:select(nil,
            {limit=2, fetch_pos=true, return_raw=true})
    t.assert_equals(tuples:decode(), {})
    t.assert_equals(pos, nil)

    cg.server:exec(function()
        for i = 1, 11 do
            box.space.s:replace{i, 1}
        end
    end)
    local tuples1
    local tuples2
    local tuples_offset
    local pos = ""
    local last_tuple = box.NULL

    -- Test fullscan pagination
    for i = 0, 5 do
        tuples1, pos = s:select(nil,
            {limit=2, fetch_pos=true, after=pos, return_raw=true})
        tuples1 = tuples1:decode()
        -- Check if pos is not decoded
        t.assert_equals(type(pos), 'string')
        tuples2 = s:select(nil, {limit=2, after=last_tuple, return_raw=true})
        tuples2 = tuples2:decode()
        last_tuple = tuples2[#tuples2]
        tuples_offset = s:select(nil, {limit=2, offset=i*2})
        t.assert_equals(tuples1, tuples_offset)
        t.assert_equals(tuples2, tuples_offset)
    end
    tuples1, pos = s:select(nil,
            {limit=2, fetch_pos=true, after=pos, return_raw=true})
    t.assert_equals(tuples1:decode(), {})
    t.assert_equals(pos, nil)
end

net_g.test_net_box_buffer = function(cg)
    cg.server:exec(function()
        local s = box.space.s
        s:create_index("pk", {type = "tree"})
        s:create_index("sk", {parts = {2}, type = "tree", unique=false})
    end)
    local net_box = require('net.box')
    local conn = net_box.connect(cg.server.net_box_uri)
    local s = conn.space.s

    local mp = require('msgpack')
    local buffer = require('buffer')
    local ibuf = buffer.ibuf()

    -- Fetch position in empty space
    local data_size = s:select(nil,
            {limit=2, fetch_pos=true, buffer=ibuf})
    local data = mp.decode(ibuf.rpos, data_size)
    -- Empty MP_BIN is returned as empty position
    t.assert_equals(data, {[48] = {}})
    ibuf:reset()
    cg.server:exec(function()
        for i = 1, 11 do
            box.space.s:replace{i, 1}
        end
    end)
    local tuples1
    local tuples2
    local tuples_offset
    local pos = ""
    local last_tuple = box.NULL

    -- Test fullscan pagination
    for i = 0, 5 do
        data_size = s:select(nil,
            {limit=2, fetch_pos=true, after=pos, buffer=ibuf})
        data = mp.decode(ibuf.rpos, data_size)
        tuples1 = data[48]
        pos = data[0x35]
        t.assert_not_equals(pos, nil)
        ibuf:reset()
        -- Position must have MP_BIN header, which was omitted on decoding.
        pos = string.char(0xc4) .. string.char(#pos) .. pos
        data_size = s:select(nil, {limit=2, after=last_tuple, buffer=ibuf})
        data = mp.decode(ibuf.rpos, data_size)
        tuples2 = data[48]
        ibuf:reset()
        last_tuple = tuples2[#tuples2]
        tuples_offset = s:select(nil, {limit=2, offset=i*2})
        t.assert_equals(tuples1, tuples_offset)
        t.assert_equals(tuples2, tuples_offset)
    end
    data_size = s:select(nil,
            {limit=2, fetch_pos=true, after=pos, buffer=ibuf})
    data = mp.decode(ibuf.rpos, data_size)
    tuples1 = data[48]
    pos = data[0x35]
    t.assert_equals(tuples1, {})
    t.assert_equals(pos, nil)
    ibuf:reset()
end

net_g.test_net_box_skip_header = function(cg)
    cg.server:exec(function()
        local s = box.space.s
        s:create_index("pk", {type = "tree"})
        s:create_index("sk", {parts = {2}, type = "tree", unique=false})
    end)
    local net_box = require('net.box')
    local conn = net_box.connect(cg.server.net_box_uri)
    local s = conn.space.s

    local mp = require('msgpack')
    local buffer = require('buffer')
    local ibuf = buffer.ibuf()

    -- Fetch position in empty space
    local data_size, pos = s:select(nil,
            {limit=2, fetch_pos=true, buffer=ibuf, skip_header=true})
    local data = mp.decode(ibuf.rpos, data_size)
    t.assert_equals(data, {})
    t.assert_equals(pos, nil)
    ibuf:reset()
    cg.server:exec(function()
        for i = 1, 11 do
            box.space.s:replace{i, 1}
        end
    end)
    local tuples1
    local tuples2
    local tuples_offset
    local pos = ""
    local last_tuple = box.NULL

    -- Test fullscan pagination
    for i = 0, 5 do
        data_size, pos = s:select(nil,
            {limit=2, fetch_pos=true, after=pos, buffer=ibuf, skip_header=true})
        tuples1 = mp.decode(ibuf.rpos, data_size)
        t.assert_not_equals(pos, nil)
        ibuf:reset()
        data_size = s:select(nil,
            {limit=2, after=last_tuple, buffer=ibuf, skip_header=true})
        tuples2 = mp.decode(ibuf.rpos, data_size)
        ibuf:reset()
        last_tuple = tuples2[#tuples2]
        tuples_offset = s:select(nil, {limit=2, offset=i*2})
        t.assert_equals(tuples1, tuples_offset)
        t.assert_equals(tuples2, tuples_offset)
    end
    data_size, pos = s:select(nil,
            {limit=2, fetch_pos=true, after=pos, buffer=ibuf, skip_header=true})
    tuples1 = mp.decode(ibuf.rpos, data_size)
    t.assert_equals(tuples1, {})
    t.assert_equals(pos, nil)
    ibuf:reset()
end

net_g.test_net_box_invalid_position = function(cg)
    cg.server:exec(function()
        local s = box.space.s
        s:create_index('pk', {parts={{field=1, type ='uint'}}, type ='tree'})
        s:create_index('sk', {
                parts = {{field = 2, type = 'uint', is_nullable=true}},
                type = 'tree', unique=false})
    end)
    local net_box = require('net.box')
    local conn = net_box.connect(cg.server.net_box_uri)
    local s = conn.space.s
    local sk = s.index.sk
    s:replace{1, 0}
    s:replace{2, 0}
    local _, pos = sk:select(nil, {limit=1, fetch_pos=true})
    local msg = "Iterator position is invalid"
    t.assert_error_msg_equals(msg, s.select, s, nil, {after=pos})
    _, pos = s:select(nil, {limit=1, fetch_pos=true})
    t.assert_error_msg_equals(msg, sk.select, sk, nil, {after=pos})
    t.assert_error_msg_equals(msg, sk.select, sk, nil, {after={0, 'Zero'}})
    msg = "Illegal parameters, options parameter 'after'" ..
        " should be of type string, table, tuple"
    t.assert_error_msg_equals(msg, sk.select, sk, nil, {after=15})
    t.assert_equals(sk:select(nil, {after={1}}), s:select())
end

net_g.before_test('test_pagination_not_supported', function(cg)
    cg.server:exec(function()
        box.space.s:create_index('pk')
        box.error.injection.set('ERRINJ_IPROTO_FLIP_FEATURE', 4)
    end)
end)

net_g.after_test('test_pagination_not_supported', function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_FLIP_FEATURE', -1)
    end)
end)

-- Checks that if the server doesn't report the 'pagination' feature,
-- pagination will throw an error.
net_g.test_pagination_not_supported = function(cg)
    t.tarantool.skip_if_not_debug()
    local conn = net.connect(cg.server.net_box_uri)
    t.assert_not_covers(conn.peer_protocol_features, {pagination = true})
    t.assert_error_msg_equals("Remote server does not support pagination",
        conn.space.s.select, conn.space.s, nil, {after={0}})
    t.assert_error_msg_equals("Remote server does not support pagination",
        conn.space.s.select, conn.space.s, nil, {after=""})
    t.assert_error_msg_equals("Remote server does not support pagination",
        conn.space.s.select, conn.space.s, nil, {fetch_pos=true})
    t.assert_error_msg_equals("Remote server does not support pagination",
        conn.space.s.select, conn.space.s, nil, {fetch_pos=true, after={0}})
    t.assert_error_msg_equals("Remote server does not support pagination",
        conn.space.s.select, conn.space.s, nil, {fetch_pos=true, after=""})
    -- Check if an usual select is not affected.
    conn.space.s:select(nil)
    conn:close()
end
