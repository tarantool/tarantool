local t = require('luatest')

local server = require('luatest.server')

local g = t.group(nil, t.helpers.matrix{engine = {'memtx', 'vinyl'}})

g.before_all(function(cg)
    -- Memtx MVCC is required for memtx test to do a concurrent write
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    cg.server:start()
    cg.server:exec(function(engine)
        box.schema.space.create('test', {engine = engine})
        box.space.test:format({{'field1', 'unsigned'}})
        box.space.test:create_index('pk')
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.space.test:truncate()
    end)
end)

-- The case covers a bug when SQL count didn't begin transaction
-- in engine if it was the first statement.
g.test_sql_count_does_not_begin_txn_in_engine = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        -- Open a transaction and count tuples
        box.begin()
        local count = box.execute([[SELECT COUNT() FROM SEQSCAN "test"]])
        t.assert_equals(count.rows, {{0}})

        -- Make a concurrent insert
        local f = fiber.create(function()
            box.space.test:insert{1}
        end)
        f:set_joinable(true)

        -- Wait for write to be committed and count tuples again
        f:join()
        local count = box.execute([[SELECT COUNT() FROM SEQSCAN "test"]])
        t.assert_equals(count.rows, {{0}})
        box.commit()
    end)
end

-- The case covers a problem when SQL didn't check if the count
-- was successful.
g.test_sql_count_does_not_handle_error = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        -- Make aborted-by-conflict transaction so that count will fail.
        box.begin()
        box.space.test:insert{1}

        -- Conflicting insert will abort our transaction.
        local f = fiber.create(function()
            box.space.test:insert{1}
        end)
        f:set_joinable(true)
        t.assert_equals({f:join()}, {true})

        -- Now transaction must be aborted by conflict so count will fail.
        local _, err = box.execute([[SELECT COUNT() FROM SEQSCAN "test"]])
        t.assert_not_equals(err, nil)
        t.assert_equals(err.message, 'Transaction has been aborted by conflict')
        box.rollback()
    end)
end
