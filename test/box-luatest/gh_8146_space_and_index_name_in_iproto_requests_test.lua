local fio = require('fio')
local msgpack = require('msgpack')
local netbox = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix{connection = {'connection', 'stream'}})

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks contents of net.box connections with disabled schema fetching.
g.test_net_box_conn_with_disabled_schema_fetching = function(cg)
    t.skip_if(cg.params.connection == 'stream')

    local c = netbox:connect(cg.server.net_box_uri, {fetch_schema = false})
    t.assert_equals(c.space, {})
    t.assert_not_equals(getmetatable(c.space).__index, nil)
    local s = c.space.s
    t.assert_equals(c.space, {s = s})
    t.assert_equals(s, {_id_or_name = 's', name = 's', index = {}})
    t.assert_not_equals(getmetatable(s.index).__index, nil)
    local i = s.index.i
    t.assert_equals(s.index, {i = i})
    t.assert_equals(i, {_id_or_name = 'i', name = 'i', space = s})

    local s512 = c.space[512]
    t.assert_equals(c.space, {[512] = s512, s = s})
    t.assert_equals(s512, {_id_or_name = 512, id = 512, index = {}})
    local i1 = s.index[1]
    t.assert_equals(s.index, {[1] = i1, i = i})
    t.assert_equals(i1, {_id_or_name = 1, id = 1, space = s})

    t.assert_equals(c.space[{}], nil)
    t.assert_equals(c.space[true], nil)
    t.assert_equals(c.space[function() end], nil)
    t.assert_equals(c.space.s.index[{}], nil)
    t.assert_equals(c.space.s.index[true], nil)
    t.assert_equals(c.space.s.index[function() end], nil)
end

g.before_test('test_net_box_conn_with_disabled_schema_fetching_errinj',
              function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_FLIP_FEATURE',
                                box.iproto.feature.space_and_index_names)
    end)
end)

-- Checks contents of net.box connections with disabled schema fetching when
-- space and index names IPROTO feature is disabled via error injection.
g.test_net_box_conn_with_disabled_schema_fetching_errinj = function(cg)
    t.tarantool.skip_if_not_debug()
    t.skip_if(cg.params.connection == 'stream')

    local c = netbox:connect(cg.server.net_box_uri, {fetch_schema = false})

    t.assert_equals(c.space.s, nil)

    local s512 = c.space[512]
    t.assert_equals(c.space, {[512] = s512})
    t.assert_equals(s512, {_id_or_name = 512, id = 512, index = {}})
    t.assert_equals(s512.index.i, nil)
    local i1 = s512.index[1]
    t.assert_equals(s512.index, {[1] = i1})
    t.assert_equals(i1, {_id_or_name = 1, id = 1, space = s512})
end

g.after_test('test_net_box_conn_with_disabled_schema_fetching_errinj',
              function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_IPROTO_FLIP_FEATURE', -1)
    end)
end)

g.before_test('test_net_box_conn_space_and_index_wrapper_tables', function(cg)
    cg.s_id = cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('i')
        s:create_index('s', {parts = {2, 'unsigned'}})
        s:insert{0, 1}
        s:insert{1, 0}
        local s_space = box.schema.create_space(' s ')
        s_space:create_index(' i ')
        s_space:insert{0}
        return s.id
    end)
end)

-- Checks that space and index wrapper tables of net.box connections with
-- disabled schema fetching work correctly.
g.test_net_box_conn_space_and_index_wrapper_tables = function(cg)
    local c = netbox:connect(cg.server.net_box_uri, {fetch_schema = false})
    if cg.params.connection == 'stream' then
        c = c:new_stream()
    end
    local s = c.space.s
    local i = s.index.i

    t.assert_equals(s:select(), {{0, 1}, {1, 0}})
    t.assert_equals(s:insert{2, 2}, {2, 2})
    t.assert_equals(s:update({2}, {{'=', 2, 3}}), {2, 3})
    t.assert_equals(i:update({2}, {{'=', 2, 2}}), {2, 2})
    t.assert_equals(i:select({0}), {{0, 1}})
    t.assert_equals(i:delete{2}, {2, 2})
    t.assert_equals(i:count(), 2)
    t.assert_equals(i:min(), {0, 1})
    t.assert_equals(i:max(), {1, 0})
    t.assert_equals(i:get{0}, {0, 1})

    s = c.space[cg.s_id]
    i = s.index[1]
    t.assert_equals(s:replace{1, 2}, {1, 2})
    t.assert_equals(i:select{2}, {{1, 2}})
    t.assert_equals(s:update({0}, {{'=', 2, 3}}), {0, 3})
    t.assert_equals(i:update({3}, {{'=', 2, 0}}), {0, 0})
    t.assert_equals(s:upsert({2, 4}, {{'=', 2, 5}}), nil)
    t.assert_equals(s:upsert({2, 4}, {{'=', 2, 5}}), nil)
    t.assert_equals(i:delete{5}, {2, 5})
    t.assert_equals(i:count(), 2)
    t.assert_equals(i:min(), {0, 0})
    t.assert_equals(i:max(), {1, 2})
    t.assert_equals(i:get{2}, {1, 2})

    t.assert_equals(c.space[cg.s_id].index.i:count(), 2)
    t.assert_equals(c.space.s.index[1]:count(), 2)
    t.assert_equals(c.space[' s '].index[' i ']:count(), 1)

    local err_msg = "Space 'nonexistent' does not exist"
    t.assert_error_msg_content_equals(err_msg, function()
        c.space.nonexistent:select{}
    end)
    local err_msg = "Space 'nonexistent' does not exist"
    t.assert_error_msg_content_equals(err_msg, function()
        c.space.nonexistent:insert{}
    end)
    err_msg = "Space '777' does not exist"
    t.assert_error_msg_content_equals(err_msg, function()
        c.space[777]:select{}
    end)
    err_msg = "Space '777' does not exist"
    t.assert_error_msg_content_equals(err_msg, function()
        c.space[777]:insert{}
    end)
    err_msg = "Space '777' does not exist"
    t.assert_error_msg_content_equals(err_msg, function()
        c.space[777].index.i:select{}
    end)
    err_msg = "No index 'nonexistent' is defined in space 's'"
    t.assert_error_msg_content_equals(err_msg, function()
        c.space[cg.s_id].index.nonexistent:select{}
    end)
    err_msg = "No index #777 is defined in space 's'"
    t.assert_error_msg_content_equals(err_msg, function()
        c.space[cg.s_id].index[777]:select{}
    end)
end

g.after_test('test_net_box_conn_space_and_index_wrapper_tables', function(cg)
    cg.server:exec(function()
        box.space.s:drop()
        box.space[' s ']:drop()
    end)
end)

g.before_test('test_space_and_index_name_resolution', function(cg)
    cg.s_id, cg.s1_id, cg.s2_id = cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('i1')
        s:create_index('i2', {parts = {2}})
        s:insert{1, 2}
        s:insert{2, 1}
        local s1 = box.schema.create_space('s1')
        s1:create_index('i')
        s1:insert{1}
        local s2 = box.schema.create_space('s2')
        s2:create_index('i')
        s2:insert{2}
        return s.id, s1.id
    end)
end)

local function inject_select(c, sid, space_name, iid, idx_name, key)
    local header = msgpack.encode({
        [box.iproto.key.REQUEST_TYPE] = box.iproto.type.SELECT,
        [box.iproto.key.SYNC] = c:_next_sync(),
        [box.iproto.key.STREAM_ID] = c._stream_id or 0,
    })
    local body = msgpack.encode({
        [box.iproto.key.SPACE_ID] = sid,
        [box.iproto.key.SPACE_NAME] = space_name,
        [box.iproto.key.INDEX_ID] = iid,
        [box.iproto.key.INDEX_NAME] = idx_name,
        [box.iproto.key.LIMIT] = 1,
        [box.iproto.key.KEY] = setmetatable({key}, {__serialize = 'array'}),
    })
    local size = msgpack.encode(#header + #body)
    local request = size .. header .. body
    return c:_inject(request)
end

local function inject_insert_or_replace(c, request, space_name, index_name)
    local header = msgpack.encode({
        [box.iproto.key.REQUEST_TYPE] = box.iproto.type[request],
        [box.iproto.key.SYNC] = c:_next_sync(),
        [box.iproto.key.STREAM_ID] = c._stream_id or 0,
    })
    local body = msgpack.encode({
        [box.iproto.key.SPACE_NAME] = space_name,
        [box.iproto.key.INDEX_NAME] = index_name,
        [box.iproto.key.TUPLE] = {3, 3},
    })
    local size = msgpack.encode(#header + #body)
    local request = size .. header .. body
    return c:_inject(request)
end

-- Checks that space and index name resolution works correctly.
g.test_space_and_index_name_resolution = function(cg)
    local c = netbox:connect(cg.server.net_box_uri, {fetch_schema = false})
    if cg.params.connection == 'stream' then
        c = c:new_stream()
    end

    t.assert_equals(inject_select(c, cg.s1_id, 's2'), {{2}})
    t.assert_equals(inject_select(c, 777, 's2'), {{2}})
    t.assert_equals(inject_select(c, 0, 's2'), {{2}})
    t.assert_equals(inject_select(c, cg.s2_id, 's1'), {{1}})
    t.assert_equals(inject_select(c, 777, 's1'), {{1}})
    t.assert_equals(inject_select(c, 0, 's1'), {{1}})

    t.assert_equals(inject_select(c, nil, 's', 0, 'i2', 2), {{1, 2}})
    t.assert_equals(inject_select(c, nil, 's', 777, 'i2', 2), {{1, 2}})
    t.assert_equals(inject_select(c, nil, 's', 1, 'i1', 2), {{2, 1}})
    t.assert_equals(inject_select(c, nil, 's', 777, 'i1', 2), {{2, 1}})

    local err_msg = "Space 'nonexistent' does not exist"
    t.assert_error_msg_content_equals(err_msg, function()
        inject_select(c, cg.s_id, 'nonexistent')
    end)
    err_msg = "No index 'nonexistent' is defined in space 's'"
    t.assert_error_msg_content_equals(err_msg, function()
        inject_select(c, nil, 's', 0, 'nonexistent')
    end)

    t.assert_equals(inject_insert_or_replace(c, 'INSERT', 's', 'nonexistent'),
                    {{3, 3}})
    t.assert_equals(inject_insert_or_replace(c, 'REPLACE', 's', 'nonexistent'),
                    {{3, 3}})
end

g.after_test('test_space_and_index_name_resolution', function(cg)
    cg.server:exec(function()
        box.space.s:drop()
    end)
end)

-- Checks that space name is not accepted instead of space identifier in
-- recovery requests.
--
-- Snapshot generation instruction:
-- 1. Patch this place make the replace request body contain IPROTO_SPACE_NAME
-- instead OF IPROTO_SPACE_ID:
-- luacheck: no max comment line length
-- https://github.com/tarantool/tarantool/blob/5ce3114436bc94ab8414c88e4675e3e50923c199/src/box/iproto_constants.h#L497-L499
-- For instance, add this snippet:
-- ```
--   if (space_id == 512) {
--     body->k_space_id = 0x5c;
--     body->m_space_id = 0xa4; /* 4-byte string */
--     char space_name[5] = {};
--     strcpy(space_name, "name");
--     space_id = *(uint32_t *)space_name;
--     body->v_space_id = space_id;
--   } else {
--     body->k_space_id = IPROTO_SPACE_ID;
--     body->m_space_id = 0xce; /* uint32 */
--     body->v_space_id = mp_bswap_u32(space_id);
--   }
-- ```
-- 2. Build and run Tarantool, call `box.cfg`;
-- 3. Create a user space, a primary index for it and insert a tuple into it.
-- 4. Call `box.snapshot`.
g.test_space_name_in_snapshot = function(cg)
    local s = server:new{
        alias = 'recovery_' .. cg.params.connection,
        datadir = 'test/box-luatest/gh_8146_data'
    }
    s:start{wait_until_ready = false}
    local log = fio.pathjoin(s.workdir, s.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert_not_equals(s:grep_log("can't initialize storage: " ..
                                       "Missing mandatory field 'SPACE_ID' " ..
                                       "in request", nil,
                                       {filename = log}), nil)
    end)
    s:drop()
end
