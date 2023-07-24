local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local net = require('net.box')
local msgpack = require('msgpack')

local g = t.group('gh_8650')

g.before_each(function(cg)
    cg.cluster = cluster:new({})
    local box_cfg = {
        memtx_use_mvcc_engine = true,
        replication = {
            server.build_listen_uri('master', cg.cluster.id),
            server.build_listen_uri('replica', cg.cluster.id)
        },
        replication_synchro_quorum = 2
    }

    cg.master = cg.cluster:build_and_add_server({alias = 'master',
                                                 box_cfg = box_cfg})
    box_cfg.read_only = true
    cg.replica = cg.cluster:build_and_add_server({alias = 'replica',
                                                  box_cfg = box_cfg})
    cg.cluster:start()
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

g.test_box_begin_commit_is_sync = function(cg)
    cg.master:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.ctl.promote()

        box.begin()
            box.space.test:insert{1, 'is_sync = false'}
        box.commit()
        box.begin({is_sync = true})
            box.space.test:insert{2, 'is_sync = true'}
        box.commit()
        box.begin()
            box.space.test:insert{3, 'is_sync = true'}
        box.commit({is_sync = true})
        box.begin({is_sync = true})
            box.space.test:insert{4, 'is_sync = true'}
        box.commit({is_sync = true})
    end)

    local err_msg = "Illegal parameters, is_sync can only be true"
    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.begin({is_sync = false})
                box.space.test:insert{5, 'is_sync = error'}
            box.commit()
        end)
    end)

    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.begin()
                box.space.test:insert{6, 'is_sync = error'}
            box.commit({is_sync = false})
        end)
    end)

    err_msg = "Illegal parameters, is_sync must be a boolean"
    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.begin({is_sync = "foo"})
                box.space.test:insert{7, 'is_sync = error'}
            box.commit()
        end)
    end)

    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.begin()
                box.space.test:insert{8, 'is_sync = error'}
            box.commit({is_sync = "foo"})
        end)
    end)

    t.assert_equals(cg.master:exec(function()
        return box.space.test:select()
    end), {{1, 'is_sync = false'}, {2, 'is_sync = true'},
           {3, 'is_sync = true'}, {4, 'is_sync = true'}})

    t.assert_equals(cg.replica:exec(function()
        return box.space.test:select()
    end), {{1, 'is_sync = false'}, {2, 'is_sync = true'},
           {3, 'is_sync = true'}, {4, 'is_sync = true'}})


    cg.replica:stop()

    -- Async transaction
    cg.master:exec(function()
        box.begin()
            box.space.test:insert{9, 'is_sync = false'}
        box.commit()
    end)

    -- Sync transaction
    err_msg = "Quorum collection for a synchronous transaction is timed out"
    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.cfg{replication_synchro_timeout = 0.01}
            box.begin({is_sync = true})
                box.space.test:insert{10, 'is_sync = true'}
            box.commit()
        end)
    end)

    t.assert_equals(cg.master:exec(function()
        return box.space.test:select()
    end), {{1, 'is_sync = false'}, {2, 'is_sync = true'},
           {3, 'is_sync = true'}, {4, 'is_sync = true'},
           {9, 'is_sync = false'}})
end

g.test_box_atomic_is_sync = function(cg)
    cg.master:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.ctl.promote()

        box.atomic(function()
            box.space.test:insert{1, 'is_sync = false'}
            box.space.test:insert{2, 'is_sync = false'}
        end)
        box.atomic({is_sync = true}, function()
            box.space.test:insert{3, 'is_sync = true'}
            box.space.test:insert{4, 'is_sync = true'}
        end)
    end)

    local err_msg = "Illegal parameters, is_sync can only be true"
    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.atomic({is_sync = false}, function()
                box.space.test:insert{5, 'is_sync = error'}
                box.space.test:insert{6, 'is_sync = error'}
            end)
        end)
    end)

    err_msg = "Illegal parameters, is_sync must be a boolean"
    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.atomic({is_sync = "foo"}, function()
                box.space.test:insert{7, 'is_sync = error'}
                box.space.test:insert{8, 'is_sync = error'}
            end)
        end)
    end)

    t.assert_equals(cg.master:exec(function()
        return box.space.test:select()
    end), {{1, 'is_sync = false'}, {2, 'is_sync = false'},
           {3, 'is_sync = true'}, {4, 'is_sync = true'}})

    t.assert_equals(cg.replica:exec(function()
        return box.space.test:select()
    end), {{1, 'is_sync = false'}, {2, 'is_sync = false'},
           {3, 'is_sync = true'}, {4, 'is_sync = true'}})

    cg.replica:stop()

    -- Async transaction
    cg.master:exec(function()
        box.atomic(function()
            box.space.test:insert{9, 'is_sync = false'}
        end)
    end)

    -- Sync transaction
    err_msg = "Quorum collection for a synchronous transaction is timed out"
    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.cfg{replication_synchro_timeout = 0.01}
            box.atomic({is_sync = true}, function()
                box.space.test:insert{10, 'is_sync = true'}
            end)
        end)
    end)

    t.assert_equals(cg.master:exec(function()
        return box.space.test:select()
    end), {{1, 'is_sync = false'}, {2, 'is_sync = false'},
           {3, 'is_sync = true'}, {4, 'is_sync = true'},
           {9, "is_sync = false"}})
end

local function inject_iproto_begin_or_commit(c, request, is_sync)
    local header = msgpack.encode({
        [box.iproto.key.REQUEST_TYPE] = box.iproto.type[request],
        [box.iproto.key.SYNC] = c:_next_sync(),
        [box.iproto.key.STREAM_ID] = c._stream_id or 0,
    })
    local body = msgpack.encode({
        [box.iproto.key.IS_SYNC] = is_sync,
    })
    local size = msgpack.encode(#header + #body)
    local request = size .. header .. body
    return c:_inject(request)
end

g.test_iproto_is_sync = function(cg)
    local c = net.connect(cg.master.net_box_uri)
    local s = c:new_stream()

    cg.master:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.ctl.promote()
    end)

    -- Throwing iproto errors if is_sync is set incorrectly
    local err_msg = "Illegal parameters, is_sync can only be true"
    t.assert_error_msg_content_equals(err_msg, function()
        inject_iproto_begin_or_commit(s, 'BEGIN', false)
    end)

    err_msg = "Invalid MsgPack - request body"
    t.assert_error_msg_content_equals(err_msg, function()
        inject_iproto_begin_or_commit(s, 'BEGIN', 'foo')
    end)

    err_msg = "Illegal parameters, is_sync can only be true"
    t.assert_error_msg_content_equals(err_msg, function()
        inject_iproto_begin_or_commit(s, 'COMMIT', false)
    end)

    err_msg = "Invalid MsgPack - request body"
    t.assert_error_msg_content_equals(err_msg, function()
        inject_iproto_begin_or_commit(s, 'COMMIT', 'foo')
    end)

    -- is_sync is set correctly
    s:begin()
    s.space.test:insert({1, 'is_sync = false'})
    s:commit()

    s:begin({is_sync = true})
    s.space.test:insert({2, 'is_sync = true'})
    s:commit()

    s:begin()
    s.space.test:insert({3, 'is_sync = true'})
    s:commit({is_sync = true})

    cg.replica:stop()

    -- Async transaction
    s:begin()
    s.space.test:insert({4, 'is_sync = false'})
    s:commit()

    -- Sync transaction
    cg.master:exec(function()
        box.cfg{replication_synchro_timeout = 0.01}
    end)

    err_msg = "Quorum collection for a synchronous transaction is timed out"
    t.assert_error_msg_content_equals(err_msg, function()
        s:begin({is_sync = true})
        s.space.test:insert({5, 'is_sync = true'})
        s:commit()
    end)

    t.assert_error_msg_content_equals(err_msg, function()
        s:begin()
        s.space.test:insert({6, 'is_sync = true'})
        s:commit({is_sync = true})
    end)

    t.assert_equals(cg.master:exec(function()
        return box.space.test:select()
    end), {{1, 'is_sync = false'}, {2, 'is_sync = true'},
           {3, 'is_sync = true'}, {4, 'is_sync = false'}})

    c:close()
end

g.test_commit_opts = function(cg)
    local c = net.connect(cg.master.net_box_uri)
    local s = c:new_stream()

    cg.master:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.ctl.promote()
    end)

    -- Commit options are set correctly
    s:begin()
    s.space.test:insert({1, 'good'})
    s:commit({is_async = false})

    s:begin()
    s.space.test:insert({2, 'good'})
    s:commit()

    s:begin()
    s.space.test:insert({3, 'good'})
    s:commit({is_sync = true})

    s:begin()
    s.space.test:insert({4, 'good'})
    s:commit({is_sync = true}, {is_async = false})

    s:begin()
    s.space.test:insert({5, 'good'})
    s:commit(nil, {is_async = false})

    t.assert_equals(cg.master:exec(function()
        return box.space.test:select()
    end), {{1, 'good'}, {2, 'good'}, {3, 'good'}, {4, 'good'}, {5, 'good'}})

    -- Commit options are set incorrectly
    s:begin()
    local err_msg = "Illegal parameters, unexpected option 'is_sync'"
    t.assert_error_msg_content_equals(err_msg, function()
        s:commit({is_sync = true, is_async = false})
    end)
    t.assert_error_msg_content_equals(err_msg, function()
        s:commit({is_async = false, is_sync = false})
    end)

    err_msg = "Illegal parameters, options are either in the wrong order or " ..
              "mixed up"
    t.assert_error_msg_content_equals(err_msg, function()
        s:commit({is_async = false}, {is_sync = false})
    end)

    err_msg = "Illegal parameters, options should be a table"
    t.assert_error_msg_content_equals(err_msg, function()
        s:commit({}, 123)
    end)
    t.assert_error_msg_content_equals(err_msg, function()
        s:commit(123, {})
    end)

    c:close()
end

g.test_commit_local_space = function(cg)
    cg.master:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.schema.space.create('loc', {is_local = true})
        box.space.loc:create_index('pk')
        box.ctl.promote()
        box.begin() box.space.loc:insert{1} box.commit()
    end)


    local err_msg = "An attempt to commit a fully local transaction " ..
                    "synchronously"
    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.begin({is_sync = true}) box.space.loc:insert{2} box.commit()
        end)
    end)

    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.begin() box.space.loc:insert{3} box.commit({is_sync = true})
        end)
    end)

    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.begin({is_sync = true})
                box.space.loc:insert{4}
            box.commit({is_sync = true})
        end)
    end)

    t.assert_error_msg_content_equals(err_msg, function()
        cg.master:exec(function()
            box.atomic({is_sync = true}, function()
                box.space.loc:insert{5}
            end)
        end)
    end)

    t.assert_equals(cg.master:exec(function()
        return box.space.loc:select()
    end), {{1}})
end
