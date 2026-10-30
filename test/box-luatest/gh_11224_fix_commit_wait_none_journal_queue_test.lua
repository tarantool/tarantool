local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_test('test_box_commit_non_empty_journal_queue', function(cg)
    cg.server:exec(function()
        box.cfg{wal_queue_max_size = box.NULL}
    end)
end)

g.test_box_commit_non_empty_journal_queue = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.create_space('test')
        s:create_index('pk')
        box.cfg{wal_queue_max_size = 100}

        local yielded
        local commit_ok
        local commit_err
        fiber.create(function()
            box.begin()
            box.on_commit(function()
                -- 3. When 1. is written to WAL queue size is decreased
                -- but 2. is still in queue so `wait = 'none'` should fail.
                fiber.create(function()
                    box.begin()
                    s:insert({3})
                    local csw = fiber.self().info().csw
                    commit_ok, commit_err = pcall(box.commit, {wait = 'none'})
                    yielded = fiber.self().info().csw ~= csw
                end)
            end)
            --- 1. Insert large tuple to block journal queue.
            s:insert({1, string.rep('a', 1000)})
            box.commit()
        end)
        -- 2. Next insertion will wait in journal queue.
        s:insert({2})

        t.assert_not(yielded)
        t.assert_not(commit_ok)
        t.assert_covers(commit_err:unpack(), {
            type = 'ClientError',
            code = box.error.WAL_QUEUE_FULL,
            message = "The WAL queue is full",
        })
        t.assert_equals(s:select({1}, {iterator = 'gt'}), {{2}})
    end)
end
