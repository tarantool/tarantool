test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
--
-- gh-1012: Indexes for JSON-defined paths.
--
s = box.schema.space.create('withdata', {engine = engine})
-- Test build field tree conflicts.
s:create_index('test1', {parts = {{2, 'number'}, {3, 'str', path = 'FIO["fname"]'}, {3, 'str', path = '["FIO"].fname'}}})
s:create_index('test1', {parts = {{2, 'number'}, {3, 'str', path = 666}, {3, 'str', path = '["FIO"]["fname"]'}}})
s:create_index('test1', {parts = {{2, 'number'}, {3, 'map', path = 'FIO'}}})
s:create_index('test1', {parts = {{2, 'number'}, {3, 'array', path = '[1]'}}})
s:create_index('test1', {parts = {{2, 'number'}, {3, 'str', path = 'FIO'}, {3, 'str', path = 'FIO.fname'}}})
s:create_index('test1', {parts = {{2, 'number'}, {3, 'str', path = '[1].sname'}, {3, 'str', path = '["FIO"].fname'}}})
s:create_index('test1', {parts = {{2, 'number'}, {3, 'str', path = 'FIO....fname'}}})
idx = s:create_index('test1', {parts = {{2, 'number'}, {3, 'str', path = 'FIO.fname', is_nullable = false}, {3, 'str', path = '["FIO"]["sname"]'}}})
idx ~= nil
idx.parts[2].path == 'FIO.fname'
-- Test format mismatch.
format = {{'id', 'unsigned'}, {'meta', 'unsigned'}, {'data', 'array'}, {'age', 'unsigned'}, {'level', 'unsigned'}}
s:format(format)
format = {{'id', 'unsigned'}, {'meta', 'unsigned'}, {'data', 'map'}, {'age', 'unsigned'}, {'level', 'unsigned'}}
s:format(format)
s:create_index('test2', {parts = {{2, 'number'}, {3, 'number', path = 'FIO.fname'}, {3, 'str', path = '["FIO"]["sname"]'}}})
-- Test incompatable tuple insertion.
s:insert{7, 7, {town = 'London', FIO = 666}, 4, 5}
s:insert{7, 7, {town = 'London', FIO = {fname = 666, sname = 'Bond'}}, 4, 5}
s:insert{7, 7, {town = 'London', FIO = {fname = "James"}}, 4, 5}
s:insert{7, 7, {town = 'London', FIO = {fname = 'James', sname = 'Bond'}}, 4, 5}
s:insert{7, 7, {town = 'London', FIO = {fname = 'James', sname = 'Bond'}}, 4, 5}
s:insert{7, 7, {town = 'London', FIO = {fname = 'James', sname = 'Bond', data = "extra"}}, 4, 5}
s:insert{7, 7, {town = 'Moscow', FIO = {fname = 'Max', sname = 'Isaev', data = "extra"}}, 4, 5}
idx:select()
idx:min()
idx:max()
s:drop()

-- Test user-friendly index creation interface.
s = box.schema.space.create('withdata', {engine = engine})
format = {{'data', 'map'}, {'meta', 'str'}}
s:format(format)
s:create_index('pk_invalid', {parts = {{']sad.FIO["sname"]', 'str'}}})
s:create_index('pk_unexistent', {parts = {{'unexistent.FIO["sname"]', 'str'}}})
pk = s:create_index('pk', {parts = {{'data.FIO["sname"]', 'str'}}})
pk ~= nil
sk2 = s:create_index('sk2', {parts = {{'["data"].FIO["sname"]', 'str'}}})
sk2 ~= nil
sk3 = s:create_index('sk3', {parts = {{'[\'data\'].FIO["sname"]', 'str'}}})
sk3 ~= nil
sk4 = s:create_index('sk4', {parts = {{'[1].FIO["sname"]', 'str'}}})
sk4 ~= nil
pk.fieldno == sk2.fieldno
sk2.fieldno == sk3.fieldno
sk3.fieldno == sk4.fieldno
pk.path == sk2.path
sk2.path == sk3.path
sk3.path == sk4.path
s:insert{{town = 'London', FIO = {fname = 'James', sname = 'Bond'}}, "mi6"}
s:insert{{town = 'Moscow', FIO = {fname = 'Max', sname = 'Isaev', data = "extra"}}, "test"}
pk:get({'Bond'}) == sk2:get({'Bond'})
sk2:get({'Bond'}) == sk3:get({'Bond'})
sk3:get({'Bond'}) == sk4:get({'Bond'})
s:drop()

-- Test upsert of JSON-indexed data.
s = box.schema.create_space('withdata', {engine = engine})
parts = {}
parts[1] = {1, 'unsigned', path='[2]'}
pk = s:create_index('pk', {parts = parts})
s:insert{{1, 2}, 3}
s:upsert({{box.null, 2}}, {{'+', 2, 5}})
s:get(2)
s:drop()

-- Test index creation on space with data.
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('primary', { type = 'tree', parts = {{2, 'number'}} })
s:insert{1, 1, 7, {town = 'London', FIO = 1234}, 4, 5}
s:insert{2, 2, 7, {town = 'London', FIO = {fname = 'James', sname = 'Bond'}}, 4, 5}
s:insert{3, 3, 7, {town = 'London', FIO = {fname = 'James', sname = 'Bond'}}, 4, 5}
s:insert{4, 4, 7, {town = 'London', FIO = {1,2,3}}, 4, 5}
s:create_index('test1', {parts = {{3, 'number'}, {4, 'str', path = '["FIO"]["fname"]'}, {4, 'str', path = '["FIO"]["sname"]'}}})
_ = s:delete(1)
s:create_index('test1', {parts = {{3, 'number'}, {4, 'str', path = '["FIO"]["fname"]'}, {4, 'str', path = '["FIO"]["sname"]'}}})
_ = s:delete(2)
s:create_index('test1', {parts = {{3, 'number'}, {4, 'str', path = '["FIO"]["fname"]'}, {4, 'str', path = '["FIO"]["sname"]'}}})
_ = s:delete(4)
idx = s:create_index('test1', {parts = {{3, 'number'}, {4, 'str', path = '["FIO"]["fname"]', is_nullable = true}, {4, 'str', path = '["FIO"]["sname"]'}, {4, 'str', path = '["FIO"]["extra"]', is_nullable = true}}})
idx ~= nil
s:create_index('test2', {parts = {{3, 'number'}, {4, 'number', path = '["FIO"]["fname"]'}}})
idx2 = s:create_index('test2', {parts = {{3, 'number'}, {4, 'str', path = '["FIO"]["fname"]'}}})
idx2 ~= nil
t = s:insert{5, 5, 7, {town = 'Matrix', FIO = {fname = 'Agent', sname = 'Smith'}}, 4, 5}
idx:select()
idx:min()
idx:max()
idx:drop()
s:drop()

-- Test complex JSON indexes with nullable fields.
s = box.schema.space.create('withdata', {engine = engine})
parts = {}
parts[1] = {1, 'str', path='[3][2].a'}
parts[2] = {1, 'unsigned', path = '[3][1]'}
parts[3] = {2, 'str', path = '[2].d[1]'}
pk = s:create_index('primary', { type = 'tree', parts =  parts})
s:insert{{1, 2, {3, {3, a = 'str', b = 5}}}, {'c', {d = {'e', 'f'}, e = 'g'}}, 6, {1, 2, 3}}
s:insert{{1, 2, {3, {a = 'str', b = 1}}}, {'c', {d = {'e', 'f'}, e = 'g'}}, 6}
parts = {}
parts[1] = {4, 'unsigned', path='[1]', is_nullable = false}
parts[2] = {4, 'unsigned', path='[2]', is_nullable = true}
parts[3] = {4, 'unsigned', path='[4]', is_nullable = true}
trap_idx = s:create_index('trap', { type = 'tree', parts = parts})
s:insert{{1, 2, {3, {3, a = 'str2', b = 5}}}, {'c', {d = {'e', 'f'}, e = 'g'}}, 6, {}}
parts = {}
parts[1] = {1, 'unsigned', path='[3][2].b' }
parts[2] = {3, 'unsigned'}
crosspart_idx = s:create_index('crosspart', { parts =  parts})
s:insert{{1, 2, {3, {a = 'str2', b = 2}}}, {'c', {d = {'e', 'f'}, e = 'g'}}, 6, {9, 2, 3}}
parts = {}
parts[1] = {1, 'unsigned', path='[3][2].b'}
num_idx = s:create_index('numeric', {parts =  parts})
s:insert{{1, 2, {3, {a = 'str3', b = 9}}}, {'c', {d = {'e', 'f'}, e = 'g'}}, 6, {0}}
num_idx:get(2)
num_idx:select()
num_idx:max()
num_idx:min()
crosspart_idx:max() == num_idx:max()
crosspart_idx:min() == num_idx:min()
trap_idx:max()
trap_idx:min()
s:drop()

-- Test index alter.
s = box.schema.space.create('withdata', {engine = engine})
pk_simplified = s:create_index('primary', { type = 'tree',  parts = {{1, 'unsigned'}}})
pk_simplified.path == box.NULL
idx = s:create_index('idx', {parts = {{2, 'integer', path = 'a'}}})
s:insert{31, {a = 1, aa = -1}}
s:insert{22, {a = 2, aa = -2}}
s:insert{13, {a = 3, aa = -3}}
idx:select()
idx:alter({parts = {{2, 'integer', path = 'aa'}}})
idx:select()
s:drop()

-- Incompatible format change.
s = box.schema.space.create('withdata')
i = s:create_index('pk', {parts = {{1, 'integer', path = '[1]'}}})
s:insert{{-1}}
i:alter{parts = {{1, 'string', path = '[1]'}}}
s:insert{{'a'}}
i:drop()
i = s:create_index('pk', {parts = {{1, 'integer', path = '[1].FIO'}}})
s:insert{{{FIO=-1}}}
i:alter{parts = {{1, 'integer', path = '[1][1]'}}}
i:alter{parts = {{1, 'integer', path = '[1].FIO[1]'}}}
s:drop()

-- Test snapshotting and recovery.
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('pk', {parts = {{1, 'integer'}, {3, 'string', path = 'town'}}})
name = s:create_index('name', {parts = {{3, 'string', path = 'FIO.fname'}, {3, 'string', path = 'FIO.sname'}, {3, 'string', path = 'FIO.extra', is_nullable = true}}})
s:insert{1, 1, {town = 'Moscow', FIO = {fname = 'Max', sname = 'Isaev'}}}
s:insert{1, 777, {town = 'London', FIO = {fname = 'James', sname = 'Bond'}}}
s:insert{1, 45, {town = 'Berlin', FIO = {fname = 'Richard', sname = 'Sorge'}}}
s:insert{4, 45, {town = 'Berlin', FIO = {fname = 'Max', extra = 'Otto', sname = 'Stierlitz'}}}
pk:select({1})
pk:select({1, 'Berlin'})
name:select({})
name:select({'Max'})
name:get({'Max', 'Stierlitz', 'Otto'})
box.snapshot()
test_run:cmd("restart server default")
engine = test_run:get_cfg('engine')
s = box.space["withdata"]
pk = s.index["pk"]
name = s.index["name"]
pk:select({1})
pk:select({1, 'Berlin'})
name:select({})
name:select({'Max'})
name:get({'Max', 'Stierlitz', 'Otto'})
s:replace{4, 45, {town = 'Berlin', FIO = {fname = 'Max', sname = 'Stierlitz'}}}
name:select({'Max', 'Stierlitz'})
town = s:create_index('town', {unique = false, parts = {{3, 'string', path = 'town'}}})
town:select({'Berlin'})
_ = s:delete({4, 'Berlin'})
town:select({'Berlin'})
s:update({1, 'Berlin'}, {{"+", 2, 45}})
box.snapshot()
s:upsert({1, 90, {town = 'Berlin', FIO = {fname = 'X', sname = 'Y'}}}, {{'+', 2, 1}})
town:select()
name:drop()
town:select()
s:drop()

-- Check replace with tuple with map having numeric keys that
-- cannot be included in JSON index.
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('pk', {parts={{1, 'int'}}})
idx0 = s:create_index('idx0', {parts = {{2, 'str', path = 'name'}, {3, "str"}}})
s:insert({4, {"d", name='D'}, "test"})
s:replace({4, {"d1", name='D1'}, "test"})
idx0:drop()
s:truncate()
idx0 = s:create_index('idx2', {parts = {{3, 'str', path = '[1].fname'}, {3, 'str', path = '[1].sname'}}})
s:insert({5, {1, 1, 1}, {{fname='A', sname='B'}, {fname='C', sname='D'}, {fname='A', sname='B'}}})
_ = s:delete(5)
s:drop()

-- Check that null isn't allowed in case array/map is expected
-- according to json document format.
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {{'[2][1].a', 'unsigned'}}})
s:insert{1, box.NULL} -- error
s:insert{2, {box.NULL}} -- error
s:insert{3} -- error
s:insert{4, {}} -- error
s:insert{5, {{b = 1}}} -- error
s:insert{6, {{a = 1}}} -- ok
s.index.sk:alter{parts = {{'[2][1].a', 'unsigned', is_nullable = true}}}
s:insert{7, box.NULL} -- ok
s:insert{8, {box.NULL}} -- ok
-- Skipping nullable fields is also okay.
s:insert{9} -- ok
s:insert{10, {}} -- ok
s:insert{11, {{b = 1}}} -- ok
s:insert{12, {{a = box.NULL}}} -- ok
s.index.sk:select()
s:drop()

--
-- gh-4520: Nullable fields in JSON index are not working with a
--          space having a format defined.
--
s = box.schema.space.create('x', {engine = engine})
pk = s:create_index('pk')
_ = s:create_index( '_rawdata', { type='tree', unique=false, parts={{ 5, 'scalar', path='pay_date_to', is_nullable=true }} } )
_ = s:insert{6, 1569246252, 2, 77, { f1 = 123, pay_date_to = box.NULL }, 21, 1, 361 }
s:format({{type='any', name='1'}, {type='any', name='2'}, {type='any', name='3'}, {type='any', name='4'}, {type='map', name='_rawdata', is_nullable=true}})
s:drop()

s = box.schema.space.create('sp', {engine = engine, format = {{type='string', name='key'}, {type='map', name='value', is_nullable=true}}})
_ = s:create_index('pk', {parts = {'key'}})
_ = s:create_index('clid', {parts = {{'value.clid', 'str', is_nullable = true}}})
_ = s:insert({'01', {clid = 'AA', cltp = 20}})
_ = s:insert({'02', {cltp = 'BB'}})
s:drop()
