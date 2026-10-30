local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new{alias = 'master'}
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- Since gh-7240 was fixed, writing transactions are aborted on conflict
-- immediately instead of being sent to read view and aborted on commit.
-- This test checks the following corner case:
-- 1. The first DML statement in a transaction yields reading disk to check
--    the uniqueness constraint. The transaction is technically read-only at
--    this point, because it hasn't added any statements to its write set yet.
-- 2. Another transaction updates the tuple checked by the first transaction.
--    Since the first transaction is read-only, it isn't aborted, but sent to
--    read view.
-- 3. The first transaction completes the uniqueness check successful and
--    tries to commit its write set. It should be aborted at this point,
--    because it's in read view.
--
g.test_abort_yielding_dml = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local s = box.schema.create_space('s', {engine = 'vinyl'})
        -- Disable bloom filter to enforce disk reads.
        s:create_index('pk', {bloom_fpr = 1})
        -- Make a checkpoint to enable disk reads.
        s:insert{1}
        box.snapshot()
        box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', true)
        local ch = fiber.channel(1)
        fiber.create(function()
            local ok, err = pcall(s.insert, s, {2, 20})
            ch:put(ok or tostring(err))
        end)
        -- The insert operation blocks on disk read to check uniqueness.
        t.assert_is(ch:get(0.1), nil)
        -- Abort the insert operation blocked on disk read by conflict.
        s:replace({2, 200})
        box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', false)
        t.assert_equals(ch:get(0.1), 'Transaction has been aborted by conflict')
        t.assert_equals(s:get(2), {2, 200})
        s:drop()
    end)
end
