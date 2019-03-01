test_run = require('test_run').new()

--
-- Lookup in the primary index when applying a deferred DELETE
-- for a secondary index on commit.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = { {'[1].a', 'unsigned'}, {'[1].b', 'unsigned'}, {'[1].c', 'unsigned'} }})
sk = s:create_index('sk', {unique = false, parts = {2, 'unsigned'}})
s:replace{{a = 1, b = 2, c = 3}, 10}
sk:select()
s:drop()

--
-- Lookup on INSERT to check the unique constraint.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = { {'[1].a', 'unsigned'}, {'[1].b', 'unsigned'}, {'[1].c', 'unsigned'} }})
s:replace{{a = 1, b = 2, c = 3}, 1}
box.snapshot()
s:replace{{a = 1, b = 2, c = 3}, 2}
s:insert{{a = 1, b = 2, c = 3}, 3}
pk:stat().disk.iterator.lookup -- 0 (served from memory)
s:drop()

--
-- Gap locks coalescing.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = { {'[1].a', 'unsigned'}, {'[1].b', 'unsigned'}, {'[1].c', 'unsigned'} }})
s:replace{{a = 1, b = 1, c = 1}}
s:replace{{a = 1, b = 1, c = 2}}
box.begin()
gap_locks_1 = box.stat.vinyl().tx.gap_locks
s:select({1, 1}, {iterator = 'ge', limit = 1})
s:select({1, 1}, {iterator = 'gt'})
gap_locks_2 = box.stat.vinyl().tx.gap_locks
gap_locks_2 - gap_locks_1 -- 2 (tracking intervals must not be coalesced)
box.commit()
s:drop()

--
-- Cache iterator stop condition.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = { {'[1].a', 'unsigned'}, {'[1].b', 'unsigned'}, {'[1].c', 'unsigned'} }})
s:replace{{a = 1, b = 1, c = 1}}
s:replace{{a = 1, b = 1, c = 2}}
s:replace{{a = 1, b = 1, c = 3}}
s:insert{{a = 1, b = 1, c = 3}}
s:select{1, 1, 1}
s:select{1, 1}
s:drop()
