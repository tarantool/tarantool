test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
test_run:cmd("push filter \"file: .*\" to \"file: <filename>\"")
test_run:cmd("push filter \"line: .*\" to \"line: <line>\"")

--
-- gh-1260: Func index.
--
s = box.schema.space.create('withdata', {engine = engine})
lua_code = [[function(tuple) return {tuple[1] + tuple[2]} end]]
lua_code2 = [[function(tuple) return {tuple[1] + tuple[2], 2 * tuple[1] + tuple[2]} end]]
box.schema.func.create('s_nonpersistent')
box.schema.func.create('s_ivaliddef1', {body = lua_code, is_deterministic = false, is_sandboxed = true})
box.schema.func.create('s_ivaliddef2', {body = lua_code, is_deterministic = true, is_sandboxed = false})

box.schema.func.create('s', {body = lua_code, is_deterministic = true, is_sandboxed = true})
box.schema.func.create('ss', {body = lua_code2, is_deterministic = true, is_sandboxed = true})

-- Func index can't be primary.
_ = s:create_index('idx', {func = box.func.s.id, parts = {{1, 'unsigned'}}})
pk = s:create_index('pk')
-- Invalid fid.
_ = s:create_index('idx', {func = 6666, parts = {{1, 'unsigned'}}})
s.index.idx:drop()
-- Can't use non-persistent function in functional index.
_ = s:create_index('idx', {func = box.func.s_nonpersistent.id, parts = {{1, 'unsigned'}}})
-- Can't use non-deterministic function in functional index.
_ = s:create_index('idx', {func = box.func.s_ivaliddef1.id, parts = {{1, 'unsigned'}}})
-- Can't use non-sandboxed function in functional index.
_ = s:create_index('idx', {func = box.func.s_ivaliddef2.id, parts = {{1, 'unsigned'}}})
-- Can't use non-sequential parts in returned key definition.
_ = s:create_index('idx', {func = box.func.ss.id, parts = {{1, 'unsigned'}, {3, 'unsigned'}}})
-- Can't use parts started not by 1 field.
_ = s:create_index('idx', {func = box.func.ss.id, parts = {{2, 'unsigned'}, {3, 'unsigned'}}})
-- Can't use JSON paths in returned key definiton.
_ = s:create_index('idx', {func = box.func.ss.id, parts = {{"[1]data", 'unsigned'}}})

-- Can't drop a function referenced by functional index.
idx = s:create_index('idx', {unique = true, func = box.func.s.id, parts = {{1, 'unsigned'}}})
box.schema.func.drop('s')
box.snapshot()
test_run:cmd("restart server default")
box.schema.func.drop('s')
s = box.space.withdata
idx = s.index.idx
idx:drop()
box.schema.func.drop('s')

test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

-- Invalid functional index extractor routine return: the extractor must return keys.
lua_code = [[function(tuple) return "hello" end]]
box.schema.func.create('invalidreturn0', {body = lua_code, is_deterministic = true, is_sandboxed = true})
idx = s:create_index('idx', {func = box.func.invalidreturn0.id, parts = {{1, 'unsigned'}}})
s:insert({1})
idx:drop()

-- Invalid functional index extractor routine return: a stirng instead of unsigned
lua_code = [[function(tuple) return {"hello"} end]]
box.schema.func.create('invalidreturn1', {body = lua_code, is_deterministic = true, is_sandboxed = true})
idx = s:create_index('idx', {func = box.func.invalidreturn1.id, parts = {{1, 'unsigned'}}})
s:insert({1})
idx:drop()

-- Invalid functional index extractor routine return: invalid return format for multikey index.
lua_code = [[function(tuple) return {"hello", "world"}, {1, 2} end]]
box.schema.func.create('invalidreturn2', {body = lua_code, is_deterministic = true, is_sandboxed = true, opts = {is_multikey = true}})
idx = s:create_index('idx', {func = box.func.invalidreturn2.id, parts = {{1, 'unsigned'}, {2, 'unsigned'}}})
s:insert({1})
idx:drop()

-- Invalid functional index extractor routine return: the second returned key invalid.
lua_code = [[function(tuple) return {{"hello", "world"}, {1, 2}} end]]
box.schema.func.create('invalidreturn3', {body = lua_code, is_deterministic = true, is_sandboxed = true, opts = {is_multikey = true}})
idx = s:create_index('idx', {func = box.func.invalidreturn3.id, parts = {{1, 'unsigned'}, {2, 'unsigned'}}})
s:insert({1})
idx:drop()

-- Invalid functional index extractor routine return: multikey return in case of regular index.
lua_code = [[function(tuple) return {{"hello", "world"}, {1, 2}} end]]
box.schema.func.create('invalidreturn4', {body = lua_code, is_deterministic = true, is_sandboxed = true})
idx = s:create_index('idx', {func = box.func.invalidreturn4.id, parts = {{1, 'unsigned'}, {2, 'unsigned'}}})
s:insert({1})
idx:drop()

-- Invalid functional index extractor routine return: invalid return format for multikey index 2.
lua_code = [[function(tuple) return "hello" end]]
box.schema.func.create('invalidreturn5', {body = lua_code, is_deterministic = true, is_sandboxed = true, opts = {is_multikey = true}})
idx = s:create_index('idx', {func = box.func.invalidreturn5.id, parts = {{1, 'unsigned'}, {2, 'unsigned'}}})
s:insert({1})
idx:drop()

-- Invalid function: runtime extractor error
test_run:cmd("setopt delimiter ';'")
lua_code = [[function(tuple)
                local json = require('json')
                return json.encode(tuple)
             end]]
test_run:cmd("setopt delimiter ''");
box.schema.func.create('runtimeerror', {body = lua_code, is_deterministic = true, is_sandboxed = true})
idx = s:create_index('idx', {func = box.func.runtimeerror.id, parts = {{1, 'string'}}})
s:insert({1})
e = box.error.last()
e:unpack()
e = e.prev
e:unpack()
e = e.prev
e == nil
idx:drop()

-- Remove old persistent functions
for _, v in pairs(box.func) do if v.is_persistent then box.schema.func.drop(v.name) end end
s:drop()

-- Func index test cases.
s = box.schema.space.create('withdata', {engine = engine})
lua_code = [[function(tuple) return {tuple[1] + tuple[2]} end]]
box.schema.func.create('extr', {body = lua_code, is_deterministic = true, is_sandboxed = true})
pk = s:create_index('pk')
s:insert({1, 2})
idx = s:create_index('idx', {unique = true, func = 'extr', parts = {{1, 'integer'}}})
s:insert({2, 1})
idx:get(3)
idx:delete(3)
s:select()
s:insert({2, 1})
idx:get(3)
s:drop()
box.schema.func.drop('extr')

-- Multikey functional index.
s = box.schema.space.create('withdata', {engine = engine})
lua_code = [[function(tuple) return {{tuple[1] + tuple[2]}, {tuple[1] + tuple[2]}, {tuple[1]}} end]]
box.schema.func.create('extr', {body = lua_code, is_deterministic = true, is_sandboxed = true, opts = {is_multikey = true}})
pk = s:create_index('pk')
idx = s:create_index('idx', {unique = true, func = box.func.extr.id, parts = {{1, 'integer'}}})
s:insert({1, 2})
s:insert({3, 5})
s:insert({5, 3})
idx:select()
idx:get(8)
idx:get(3)
idx:get(1)
idx:get(5)
s:drop()
box.schema.func.drop('extr')

-- Multikey multipart functional index.
s = box.schema.space.create('withdata', {engine = engine})
lua_code = [[function(tuple) return {{600 + tuple[1], 600 + tuple[2]}, {500 + tuple[1], 500 + tuple[2]}} end]]
box.schema.func.create('extr', {body = lua_code, is_deterministic = true, is_sandboxed = true, opts = {is_multikey = true}})
pk = s:create_index('pk')
idx = s:create_index('idx', {unique = true, func = box.func.extr.id, parts = {{1, 'integer'}, {2, 'integer'}}})
s:insert({1, 2})
s:insert({2, 1})
s:insert({3, 3})
idx:select({600}, {iterator = "GE"})
idx:get({603, 603})
idx:select({503}, {iterator = "LE"})
s:drop()
box.schema.func.drop('extr')

-- Multikey non-unique functional index.
s = box.schema.space.create('withdata', {engine = engine})
lua_code = [[function(tuple) return {{500 + tuple[1]}, {500 + tuple[2]}, {500 + tuple[2]}, {500 + tuple[2]}} end]]
box.schema.func.create('extr', {body = lua_code, is_deterministic = true, is_sandboxed = true, opts = {is_multikey = true}})
pk = s:create_index('pk')
idx = s:create_index('idx', {unique = false, func = box.func.extr.id, parts = {{1, 'integer'}}})
s:insert({1, 2})
s:insert({2, 1})
idx:select({501})
idx:select({502})
s:replace({1, 3})
idx:select({501})
idx:select({502})
idx:select({503})
box.snapshot()
test_run:cmd("restart server default")
s = box.space.withdata
idx = s.index.idx
idx:select({501})
idx:select({502})
idx:select({503})
s:replace({1, 2})
idx:select({501})
idx:select({502})
idx:select({503})
s:drop()
box.schema.func.drop('extr')

-- Multikey UTF-8 address extractor
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('name', {parts = {1, 'string'}})
s:insert({"James", "SIS Building Lambeth London UK"})
s:insert({"Sherlock", "221B Baker St Marylebone London NW1 6XE UK"})
-- Create functional index on space with data
test_run:cmd("setopt delimiter ';'")
lua_code = [[function(tuple)
                local address = string.split(tuple[2])
                local ret = {}
                for _, v in pairs(address) do table.insert(ret, {utf8.upper(v)}) end
                return ret
             end]]
test_run:cmd("setopt delimiter ''");
box.schema.func.create('addr_extractor', {body = lua_code, is_deterministic = true, is_sandboxed = true, opts = {is_multikey = true}})
idx = s:create_index('addr', {unique = false, func = box.func.addr_extractor.id, parts = {{1, 'string', collation = 'unicode_ci'}}})
idx:select('uk')
idx:select('Sis')
s:drop()
box.schema.func.drop('addr_extractor')

-- Partial index with functional index extractor
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('pk')
lua_code = [[function(tuple) if tuple[1] % 2 == 1 then return {{tuple[1]}} else return {} end end]]
box.schema.func.create('extr', {body = lua_code, is_deterministic = true, is_sandboxed = true, opts = {is_multikey = true}})
idx = s:create_index('idx', {unique = true, func = box.func.extr.id, parts = {{1, 'integer'}}})
s:insert({1})
s:insert({2})
s:insert({3})
s:insert({4})
idx:select()
s:drop()
box.schema.func.drop('extr')

-- Return nil from functional index extractor.
s = box.schema.space.create('withdata', {engine = engine})
pk = s:create_index('pk')
lua_code = [[function(tuple) return {nil} end]]
box.schema.func.create('extr', {body = lua_code, is_deterministic = true, is_sandboxed = true})
idx = s:create_index('idx', {unique = false, func = box.func.extr.id, parts = {{1, 'integer', is_nullable = true}}})
s:insert({1})
s:drop()
box.schema.func.drop('extr')

-- Multiple functional indexes.
s = box.schema.space.create('withdata', {engine = engine})
lua_code = [[function(tuple) return {tuple[1] + tuple[2]} end]]
box.schema.func.create('s', {body = lua_code, is_deterministic = true, is_sandboxed = true})
lua_code = [[function(tuple) return {tuple[1] - tuple[2]} end]]
box.schema.func.create('sub', {body = lua_code, is_deterministic = true, is_sandboxed = true})
pk = s:create_index('pk')
idx1 = s:create_index('s_idx', {unique = true, func = box.func.s.id, parts = {{1, 'integer'}}})
idx2 = s:create_index('sub_idx', {unique = true, func = box.func.sub.id, parts = {{1, 'integer'}}})
s:insert({4, 1})
idx1:get(5)
idx2:get(3)
idx1:drop()
idx2:get(3)
s:drop()
box.schema.func.drop('s')
box.schema.func.drop('sub')

--
-- gh-4401: make functional index creation transactional
--
test_run:cmd("setopt delimiter ';'")
function test1()
   lua_code = [[function(tuple) return {tuple[1] + tuple[2]} end]]
   box.schema.func.create('extr', {body = lua_code, is_deterministic = true, is_sandboxed = true})
   box.schema.func.create('extr1', {body = lua_code, is_deterministic = true, is_sandboxed = true})
   s = box.schema.space.create('withdata')
   pk = s:create_index('pk')
   box.space._index:insert({s.id, 2, 'idx', 'tree', {unique=true, func=box.func.extr.id}, {{0, 'integer'}}})
   box.space._func_index:insert({s.id, 2, box.func.extr1.id})
end
test_run:cmd("setopt delimiter ''");

box.atomic(test1)

box.func.extr1 == nil
box.func.extr == nil
box.is_in_txn()
box.space._space.index.name:count('withdata') == 0

-- Test successful index creation
s = box.schema.space.create('withdata', {engine = engine})
lua_code = [[function(tuple) return {tuple[1] + tuple[2]} end]]
box.schema.func.create('extr', {body = lua_code, is_deterministic = true, is_sandboxed = true})
pk = s:create_index('pk')
test_run:cmd("setopt delimiter ';'")
function test2()
    idx = s:create_index('idx', {unique = true, func = 'extr', parts = {{1, 'integer'}}})
end
test_run:cmd("setopt delimiter ''");

box.atomic(test2)

s:insert({1, 2})
idx:get({3})

s:drop()
box.func.extr:drop()

--
-- Function is added at alter.
--
s = box.schema.space.create('withdata', {engine = engine})
lua_code = [[function(tuple) return {tuple[2] >= 0 and tuple[2] or -tuple[2]} end]]
box.schema.func.create('second_field_module', {body = lua_code, is_deterministic = true, is_sandboxed = true})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {{2, 'unsigned'}}})
sk:alter({func = 'second_field_module', parts = {{1, 'unsigned'}}})
s:insert({1, -3})
sk:get{3}
s:drop()
box.schema.func.drop('second_field_module')
