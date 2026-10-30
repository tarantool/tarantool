local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug();
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
    cg.server = nil
end)

g.test_wal_error_event = function(cg)
    local conn = net.connect(cg.server.net_box_uri);
    local event = {}
    conn:watch('box.wal_error', function(k, v)
        event[k] = v
    end)
    t.helpers.retrying({}, function()
        t.assert_type(event['box.wal_error'], 'table')
    end)
    local count = event['box.wal_error'].count
    cg.server:exec(function()
        local s = box.space.test
        for i = 1, 10 do
            s:replace({i})
        end
    end)
    t.assert_equals(event['box.wal_error'], {count = count})
    cg.server:exec(function()
        local s = box.space.test
        local errinj = box.error.injection
        errinj.set('ERRINJ_WAL_FALLOCATE', 10)
        for i = 1, 10 do
            t.assert_error_msg_equals('Failed to write to disk',
                                      s.replace, s, {i})
            t.assert_equals(errinj.get('ERRINJ_WAL_FALLOCATE'), 10 - i)
        end
    end)
    count = (count or 0) + 10
    t.helpers.retrying({}, function()
        t.assert_equals(event['box.wal_error'], {count = count})
    end)
end
