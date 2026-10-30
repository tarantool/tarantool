local t = require('luatest')

local g = t.group('gh-5501', {{engine = 'memtx'},
                              {engine = 'vinyl'}})

g.before_all(function(cg)
    local server = require('luatest.server')
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
    cg.server = nil
end)

-- Check that box.commit() doesn't fail like:
-- "txn_commit_stmt: Assertion `txn->in_sub_stmt > 0' failed"
-- or "Transaction is active at return from function"
-- or "Can not commit transaction in a nested statement".
g.test_errinj = function(cg)
    local engine = cg.params.engine

    cg.server:exec(function(engine)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('pk')

        -- on_replace trigger will fail because of the error injection.
        local triggers = {
            function() pcall(s.get, s, 0) end,
            function() pcall(s.count, s, 0) end,
            function() pcall(s.pairs, s, 0) end,
            function() pcall(s.select, s, 0) end,
            function() pcall(s.index.pk.min, s.index.pk) end,
            function() pcall(s.index.pk.max, s.index.pk) end
        }

        for _, t in pairs(triggers) do
            s:on_replace(t)
            box.begin()
            box.error.injection.set("ERRINJ_INDEX_ITERATOR_NEW", true)
            s:replace{1}
            box.error.injection.set("ERRINJ_INDEX_ITERATOR_NEW", false)
            box.commit()
            s:on_replace(nil, t)
        end
    end, {engine})
end
