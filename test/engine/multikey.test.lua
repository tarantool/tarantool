test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

--
-- gh-1260: Multikey indexes.
--
s = box.schema.space.create('withdata', {engine = engine})
-- Primary index must be unique so it can't be multikey.
_ = s:create_index('idx', {parts = {{3, 'str', path = '[*].fname'}}})
pk = s:create_index('pk')
-- Test incompatible multikey index parts.
_ = s:create_index('idx3', {parts = {{3, 'str', path = '[*].fname'}, {3, 'str', path = '["data"][*].sname'}}})
_ = s:create_index('idx2', {parts = {{3, 'str', path = '[*].fname'}, {3, 'str', path = '[*].sname[*].a'}}})
_ = s:create_index('idx2', {parts = {{2, 'str', path = '[*]'}, {3, 'str', path = '[*]'}}})
idx0 = s:create_index('idx0', {parts = {{3, 'str', path = '[1].fname'}}})
_ = s:create_index('idx', {parts = {{3, 'str', path = '[*].fname'}, {3, 'str', path = '[*].sname'}}})
idx0:drop()
-- Unique multikey index.
idx = s:create_index('idx', {unique = true, parts = {{3, 'str', path = '[*].fname'}, {3, 'str', path = '[*].sname'}}})
_ = s:create_index('idx2', {parts = {{3, 'str', path = '[1].fname'}, {3, 'str', path = '[1].sname'}}})
s:insert({1, {1, 2, 3}, {{fname='James', sname='Bond'}, {fname='Vasya', sname='Pupkin'}}})
s:insert({2, {3, 4, 5}, {{fname='Ivan', sname='Ivanych'}}})
_ = s:create_index('arr_idx', {unique = true, parts = {{2, 'unsigned', path = '[*]'}}})
-- Non-unique multikey index; two multikey indexes per space.
arr_idx = s:create_index('arr_idx', {unique = false, parts = {{2, 'unsigned', path = '[*]'}}})
arr_idx:select()
idx:get({'James', 'Bond'})
idx:get({'Ivan', 'Ivanych'})
idx:get({'Vasya', 'Pupkin'})
idx:select()
s:insert({3, {1, 2}, {{fname='Vasya', sname='Pupkin'}}})
s:insert({4, {1}, {{fname='James', sname='Bond'}}})
idx:select()
-- Duplicates in multikey parts.
s:insert({5, {1, 1, 1}, {{fname='A', sname='B'}, {fname='C', sname='D'}, {fname='A', sname='B'}}})
arr_idx:select({1})
_ = s:delete(5)
-- Check that there is no garbage in index.
arr_idx:select({1})
idx:get({'A', 'B'})
idx:get({'C', 'D'})
_ = idx:delete({'Vasya', 'Pupkin'})
s:insert({6, {1, 2}, {{fname='Vasya', sname='Pupkin'}}})
s:insert({7, {1}, {{fname='James', sname='Bond'}}})
arr_idx:select({1})
idx:select()
-- Snapshot & recovery.
box.snapshot()
test_run:cmd("restart server default")
s = box.space["withdata"]
idx = s.index["idx"]
arr_idx = s.index["arr_idx"]
s:select()
idx:select()
arr_idx:select()
s:drop()

-- Assymetric multikey index paths.
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('pk')
idx = s:create_index('idx', {parts = {{3, 'str', path = '[*].fname'}, {3, 'str', path = '[*].extra.sname', is_nullable = true}}})
s:insert({1, 1, {{fname='A1', extra={sname='A2'}}, {fname='B1'}, {fname='C1', extra={sname='C2'}}}})
s:drop()

-- Unique multikey index features.
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('pk')
idx0 = s:create_index('idx0', {parts = {{2, 'int', path = '[*]'}}})
s:insert({1, {1, 1, 1}})
s:insert({2, {2, 2}})
s:insert({3, {3, 3, 2, 2, 1, 1}})
idx0:get(2)
idx0:get(1)
idx0:get(3)
idx0:select()
_ = idx0:delete(2)
idx0:get(2)
idx0:select()
s:drop()

-- Test user JSON endpoint doesn't fail in case of multikey index.
t = box.tuple.new{1, {a = 1, b = 2}, 3}
t['[2][*]']

-- Test multikey index deduplication on recovery.
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('pk')
idx0 = s:create_index('idx0', {parts = {{2, 'int', path = '[*]', is_nullable = true}}})
s:insert({1, {1, 1, 3, 1}})
idx0:select()
s:replace({1, {5, 5, 5, 1, 3, 3}})
idx0:select()
-- Test update.
s:update(1, {{'=', 2, {20, 10, 30, 30}}})
idx0:select()
box.snapshot()
test_run:cmd("restart server default")
s = box.space.withdata
s:select()
idx0 = s.index.idx0
idx0:select()
s:insert({2, {2, 4, 2}})
s:select()
idx0:select()
s:drop()

-- Test upsert & reverse iterators.
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('pk')
idx0 = s:create_index('idx0', {unique=false, parts = {{2, 'int', path = '[*]'}}})
s:insert({1, {1, 1, 1, 2, 2}})
s:insert({2, {3, 3, 4, 4, 4}})
s:insert({3, {5, 5, 5, 5, 6, 6, 6, 6}})
idx0:select(5, {iterator = box.index.LT})
idx0:select(5, {iterator = box.index.GE})
idx0:select()
s:upsert({3, {5, 5, 5, 5, 6, 6, 6, 6}}, {{'=', 2, {1, 1, 2, 2, 3, 3, 4, 4}}})
idx0:select()
s:drop()

-- Test multikey index with epsent data (1 part, is_nullable = false).
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('pk')
idx0 = s:create_index('idx0', {parts = {{2, 'str', path = '[*].name'}}})
s:insert({0, {{fname='A0'}, {fname='B0'}, {fname='C0'}}})
s:insert({0, {{fname='A0'}, {fname='B0', name='ZB1'}, {fname='C0'}}})
s:insert({0, {{fname='A0', name='ZA1'}, {fname='B0', name='ZB1'}, {fname='C0'}}})
s:insert({0, {{fname='A0', name='ZA1'}, {fname='B0', name='ZB1'}, {fname='C0', name='ZC1'}}})
s:drop()

-- Test multikey replacement conflict.
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('pk')
idx0 = s:create_index('idx0', {parts = {{2, 'str', path = '[*].name', is_nullable=true}}})
s:insert({0, {{fname='A0'}, {fname='B0'}, {fname='C0', name='CONFLICT1'}}})
s:insert({1, {{fname='A1'}, {fname='B1'}, {fname='C1', name='CONFLICT2'}}})
s:insert({2, {{fname='A2_1'}, {fname='B2_1', name='ZB2_1'}, {fname='C2_1'}, {name="DUP"}, {name="DUP"}}})
s:replace({2, {{fname='A2_2'}, {fname='B2_2', name='ZB2_2'}, {fname='C2_2'}, {name="DUP"}, {name="DUP"}}})
s:replace({2, {{fname='A2_3'}, {fname='B2_3', name='ZB2_3'}, {fname='C2_3'}, {name="DUP"}, {name='CONFLICT1'}, {name='CONFLICT2'}, {name="DUP"}}})
s:replace({2, {{fname='A2_4'}, {fname='B2_4', name='ZB2_4'}, {fname='C2_4'}, {name="DUP"}, {name='CONFLICT2'}, {name='CONFLICT1'}, {name="DUP"}}})
idx0:select()
box.snapshot()
test_run:cmd("restart server default")
s = box.space.withdata
idx0 = s.index.idx0
s:select()
idx0:select()
s:drop()

-- Test multikey index with epsent data (2 parts, is_nullable = true).
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('pk')
idx0 = s:create_index('idx0', {unique=false, parts = {{2, 'str', path = '[*].name', is_nullable=true}}})
-- No idx0 index data at all.
s:insert({0, {{fname='A0'}, {fname='B0'}, {fname='C0'}}})
idx0:select()
-- Only one field corresponds idx0.
s:insert({1, {{fname='A1'}, {fname='B1', name='ZB1'}, {fname='C1'}}})
s:insert({2, {{fname='A2'}, {fname='B2', name='ZB2'}, {fname='C2'}, {name="DUP"}, {name="DUP"}}})
idx0:select()
s:replace({1, {{fname='A3'}, {fname='B3', name='ZB3'}, {fname='C3'}}})
idx0:select()
s:replace({1, {{fname='A4', name='ZA4'}, {fname='B4', name='ZB4'}, {fname='C4', name='ZC4'}, {name="DUP"}, {name="DUP"}, {name="DUP"}}})
idx0:select()
s:replace({1, {{fname='A5', name='ZA5'}, {fname='B5'}, {fname='C5'}, {name="DUP"}, {name="DUP"}, {name="DUP"}}})
idx0:select()
s:select()
box.snapshot()
test_run:cmd("restart server default")
s = box.space.withdata
idx0 = s.index.idx0
s:select()
idx0:select()
-- Test multikey index alter.
idx0:alter({parts = {{2, 'str', path = '[*].fname', is_nullable=true}}})
idx0:select()
s:drop()

-- Create unique multikey index on space with tuple which
-- contains the same key multiple times.
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
s = box.schema.space.create('test', {engine = engine})
i = s:create_index('pk')
s:replace{1, {{1, 1}}}
s:replace{2, {{2, 3}}}
i2 = s:create_index('sk', {parts = {{2, 'unsigned', path = '[1][*]'}}})
i2:select()
s:drop()

--
-- gh-4234: Assert when using indexes containing both multikey
--          and regular key_parts.
--
s = box.schema.space.create('clients', {engine = engine})
name_idx = s:create_index('name_idx', {parts = {{1, 'string'}}})
phone_idx = s:create_index('phone_idx', {parts = {{'[2][*]', 'string'}, {3, 'string'}}, unique=false})
s:insert({"Genadiy", {"911"}, 'b'})
s:insert({"Jorge", {"911", "89457609234"}, 'a'})
s:drop()

--
-- Inserting box.NULL where a multikey array is expected is
-- handled gracefully: no crashes, just an error message.
--
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {{'[2][*]', 'unsigned'}}})
s:insert{1, box.NULL} -- ok
s:insert{2, {box.NULL}} -- error
s:insert{3, {}} -- ok
s:insert{4, {1}} -- ok
s.index.sk:alter{parts = {{'[2][*]', 'unsigned', is_nullable = true}}}
s:insert{5, box.NULL} -- ok
s:insert{6, {box.NULL}} -- ok
s:insert{7, {}} -- ok
s:insert{8, {2}} -- ok
s.index.sk:select()
s:drop()

--
-- Inserting a map where a multikey array is expected is
-- handled gracefully: no crashes, just an error message.
--
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {{'[2][*]', 'unsigned'}}})
s:insert{1, {a = 1}} -- error
s:insert{2, {1}} -- ok
s.index.sk:select()
s:drop()
