local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:stop()
end)

local function test_standalone_sequence_usage_in_yielding_transaction(engine)
    local fiber = require('fiber')

    local seq = box.schema.sequence.create('test')
    local s = box.schema.space.create('test', {engine = engine})
    s:create_index('primary')

    box.begin()
    local id = seq:next()
    fiber.yield()
    s:insert({id, 'x'})
    box.commit()

    t.assert_equals(s:get({id}), {id, 'x'})
end

g.test_standalone_sequence_usage_in_yielding_transaction_memtx = function(cg)
    local engine = 'memtx'
    cg.server:exec(test_standalone_sequence_usage_in_yielding_transaction,
                   {engine})
end

g.test_standalone_sequence_usage_in_yielding_transaction_vinyl = function(cg)
    local engine = 'vinyl'
    cg.server:exec(test_standalone_sequence_usage_in_yielding_transaction,
                   {engine})
end

local function test_standalone_sequence_usage_from_concurrent_transactions(
        engine)
    local fiber = require('fiber')

    local seq = box.schema.sequence.create('test')
    local s = box.schema.space.create('test', {engine = engine})
    s:create_index('primary')

    box.begin()
    s:insert({seq:next(), 'x'})

    local f = fiber.new(box.atomic, function()
        s:insert({seq:next(), 'y'})
    end)
    f:set_joinable(true)
    f:join()

    box.commit()

    local tuples = s:select({1}, {iterator = 'ge'})
    t.assert_equals(tuples[1], {1, 'x'})
    t.assert_equals(tuples[2], {2, 'y'})
end

g.test_standalone_sequence_usage_from_concurrent_transactions_memtx = function(
        cg)
    local engine = 'memtx'
    cg.server:exec(test_standalone_sequence_usage_from_concurrent_transactions,
                   {engine})
end

g.test_standalone_sequence_usage_from_concurrent_transactions_memtx = function(
        cg)
    local engine = 'vinyl'
    cg.server:exec(test_standalone_sequence_usage_from_concurrent_transactions,
                   {engine})
end
