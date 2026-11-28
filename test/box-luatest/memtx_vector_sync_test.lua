local server = require('luatest.server')
local t = require('luatest')

local g = t.group('memtx_vector_sync', {{mvcc = false}, {mvcc = true}})

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {
        memtx_use_mvcc_engine = cg.params.mvcc,
        replication_synchro_quorum = 1,
        replication_synchro_timeout = 0.05,
    }})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_timeout_rollback_preserves_vector_index = function(cg)
    cg.server:exec(function()
        local embedding = require('embedding')
        local s = box.schema.space.create('vector_sync', {is_sync = true})
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
        s:create_index('vec', {type = 'vector', dimension = 2,
                               unique = false, parts = {{2, 'embedding'}}})
        box.ctl.promote()
        s:insert{1, embedding.new({1, 0})}
        box.cfg{replication_synchro_quorum = 2}
        local ok = pcall(function() s:replace{1, embedding.new({0, 1})} end)
        t.assert_equals(ok, false)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.synchro.queue.len, 0)
        end)
        local rows, _, distances = s.index.vec:search({1, 0}, {
            limit = 1, with_distance = true,
        })
        t.assert_equals({rows[1][2], distances[1]},
                        {embedding.new({1, 0}), 0})
    end)
end

g.test_linearizable_count_rejected = function(cg)
    if not cg.params.mvcc then return end
    cg.server:exec(function()
        local embedding = require('embedding')
        box.cfg{replication_synchro_quorum = 1}
        local s = box.schema.space.create('vector_count_sync', {is_sync = true})
        s:format({{name = 'id', type = 'unsigned'},
                  {name = 'vec', type = 'embedding'}})
        s:create_index('pk')
        s:create_index('vec', {type = 'vector', dimension = 2,
                              unique = false, parts = {{2, 'embedding'}}})
        box.ctl.promote()
        s:insert{1, embedding.new({1, 0})}
        box.begin({txn_isolation = 'linearizable'})
        t.assert_equals(s.index.pk:count(), 1)
        t.assert_error_covers({code = box.error.VECTOR_UNSUPPORTED},
                              s.index.vec.count, s.index.vec)
        box.rollback()
        t.assert_equals(s.index.vec:count(), 1)
        s:drop()
    end)
end
