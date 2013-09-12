-- ----------------------------------------------------------------
-- LIMITS
-- ----------------------------------------------------------------
box.schema.SYSTEM_ID_MIN
box.schema.FIELD_MAX
box.schema.INDEX_FIELD_MAX
box.schema.NAME_MAX
box.schema.INDEX_ID
box.schema.SPACE_ID
box.schema.INDEX_MAX
box.schema.SPACE_MAX
box.schema.SYSTEM_ID_MAX
box.schema.SCHEMA_ID
box.schema.FORMAT_ID_MAX
-- ----------------------------------------------------------------
-- CREATE SPACE
-- ----------------------------------------------------------------

s = box.schema.create_space('tweedledum')
-- space already exists
box.schema.create_space('tweedledum')
-- create if not exists
s = box.schema.create_space('tweedledum', { if_not_exists = true })

s:drop()
-- no such space
s:drop()
-- explicit space id
s = box.schema.create_space('tweedledum', { id = 3000 })
s.n
-- duplicate id
box.schema.create_space('tweedledee', { id = 3000 })
-- stupid space id
box.schema.create_space('tweedledee', { id = 'tweedledee' })
s:drop()
-- too long space name
box.schema.create_space(string.rep('tweedledee', 100))
-- space name limit
box.schema.create_space(string.rep('t', box.schema.NAME_MAX)..'_')
s = box.schema.create_space(string.rep('t', box.schema.NAME_MAX - 1)..'_')
s.name
s:drop()
s = box.schema.create_space(string.rep('t', box.schema.NAME_MAX - 2)..'_')
s.name
s:drop()
-- space with no indexes - test update, delete, select, truncate
s = box.schema.create_space('tweedledum')
s:insert(0)
s:select(0)
s:select_range(0, 0, 0)
s:delete(0)
s:update(0, "=p", 0, 0)
s:replace(0)
s.index[0]
s:truncate()
s.enabled
-- enabled/disabled transition
s:create_index('primary', 'hash', { parts = { 0, 'num' } })
s.enabled
-- rename space - same name
s:rename('tweedledum')
s.name
-- rename space - different name
s:rename('tweedledee')
s.name
-- the reference from box.space[] to the space by old name should be gone
box.space['tweedledum']
-- rename space - bad name
s:rename(string.rep('t', box.schema.NAME_MAX * 2))
s.name
-- access to a renamed space
s:insert(0)
s:delete(0)
-- cleanup
s:drop()
-- create a space with reserved id (ok, but warns in the log)
s = box.schema.create_space('test', { id = 256 })
s.n
s:drop()
s = box.schema.create_space('test', { arity = 2 })
s.arity
s:create_index('primary', 'tree', { parts = { 0, 'num' } })
-- arity actually works
s:insert(1)
s:insert(1, 2)
s:insert(1, 2, 3)
s:select(0)
-- increase arity -- error
box.space['_space']:update(s.n, "=p", 1, 3)
s:select(0)
-- decrease arity - error
box.space['_space']:update(s.n, "=p", 1, 1)
-- remove arity - ok
box.space['_space']:update(s.n, "=p", 1, 0)
s:select(0)
-- increase arity - error
box.space['_space']:update(s.n, "=p", 1, 3)
s:truncate()
s:select(0)
-- set arity of an empty space
box.space['_space']:update(s.n, "=p", 1, 3)
s:select(0)
-- arity actually works
s:insert(3, 4)
s:insert(3, 4, 5)
s:insert(3, 4, 5, 6)
s:insert(7, 8, 9)
s:select(0)
-- check transition of space from enabled to disabled on
-- deletion of the primary key
s.enabled
s.index[0]:drop()
s.enabled
s.index[0]

-- "disabled" on
-- deletion of primary key
s:drop()
-- ----------------------------------------------------------------
-- CREATE INDEX
-- ----------------------------------------------------------------
--
s = box.schema.create_space('test')
--# setopt delimiter ';'
for k=0, box.schema.INDEX_MAX + 1, 1 do
    s:create_index('i'..k, 'hash', { parts = {0, 'num'} } )
end;
--# setopt delimiter ''
-- cleanup
for k, v in pairs (s.index) do if v.id ~= 0 then v:drop() end end
-- test limits enforced in key_def_check:
-- unknown index type
s:create_index('test', 'nosuchtype', { parts = {0, 'num'} })
-- hash index is not unique
s:create_index('test', 'hash', {parts = {0, 'num'}, unique = false })
-- bitset index is unique
s:create_index('test', 'bitset', {parts = {0, 'num'}, unique = true })
-- bitset index is multipart
s:create_index('test', 'bitset', {parts = {0, 'num', 1, 'num'}})
-- part count must be positive
s:create_index('test', 'hash', {parts = {}})
-- part count must be positive
s:create_index('test', 'hash', {parts = { 0 }})
-- unknown field type
s:create_index('test', 'hash', {parts = { 0, 'nosuchtype' }})
-- bad field no
s:create_index('test', 'hash', {parts = { 'qq', 'nosuchtype' }})
-- big field no
s:create_index('test', 'hash', {parts = { box.schema.FIELD_MAX, 'num' }})
s:create_index('test', 'hash', {parts = { box.schema.FIELD_MAX - 1, 'num' }})
s:create_index('test', 'hash', {parts = { box.schema.FIELD_MAX + 90, 'num' }})
s:create_index('test', 'hash', {parts = { box.schema.INDEX_FIELD_MAX + 1, 'num' }})
s:create_index('t1', 'hash', {parts = { box.schema.INDEX_FIELD_MAX, 'num' }})
s:create_index('t2', 'hash', {parts = { box.schema.INDEX_FIELD_MAX - 1, 'num' }})
-- cleanup
s:drop()
s = box.schema.create_space('test')
-- same part can't be indexed twice
s:create_index('t1', 'hash', {parts = { 0, 'num', 0, 'str' }})
-- a lot of key parts
parts = {}
--# setopt delimiter ';'
for k=0, box.schema.INDEX_PART_MAX, 1 do
    table.insert(parts, k)
    table.insert(parts, 'num')
end;
#parts;
s:create_index('t1', 'hash', {parts = parts});
parts = {};
for k=1, box.schema.INDEX_PART_MAX, 1 do
    table.insert(parts, k)
    table.insert(parts, 'num')
end;
#parts;
s:create_index('t1', 'hash', {parts = parts});
--# setopt delimiter ''
-- this is actually incorrect since key_field is a lua table
-- and length of a lua table which has index 0 set is not correct
#s.index[0].key_field
-- cleanup
s:drop()
-- check costraints in tuple_format_new()
s = box.schema.create_space('test')
s:create_index('t1', 'hash', { parts = { 0, 'num' }})
-- field type contradicts field type of another index
s:create_index('t2', 'hash', { parts = { 0, 'str' }})
-- ok
s:create_index('t2', 'hash', { parts = { 1, 'str' }})
-- don't allow drop of the primary key in presence of other keys
s.index[0]:drop()
-- cleanup
s:drop()
-- index name, name manipulation

-- box.schema.create_space(string.rep('t', box.schema.NAME_MAX)..'_')
-- s = box.schema.create_space(string.rep('t', box.schema.NAME_MAX - 1)..'_')
-- s.name
-- s:drop()
-- s = box.schema.create_space(string.rep('t', box.schema.NAME_MAX - 2)..'_')
-- modify index
-- ------------
--     - alter unique -> non unique
--     - alter index type
--     - add identical index - verify there is no rebuild
--     - index access by name
--     - alter add key part
--     - rename index 
--
-- build index
-- -----------
--     - index rebuild:
--        - a duplicate in the new index
--        - no field for the new index
--        - wrong field type in the new index
--
-- space cache
-- -----------
-- - all the various kinds of reference to a dropped space
--   - iterator to index
--   index to space
--   space to index
--   variable
--   key def
--   all userdata given away to lua - think how
--
--
-- -- inject error at various stages of commit and see that
-- the alter has no effects
--     - test that during commit phase
--       -> inject error at commit, inject error at rollback
--
-- usability
-- ---------
-- - space name in all error messages!
--         error: Duplicate key exists in unique index 1 (ugly)
--
-- triggers
-- --------
-- - test that after disabling triggers we can
--   create an empty snapshot
-- - test for run_triggers on/off
--
-- recovery
-- --------
--  - add primary key in snapshot
--  - add secondary key in snapshot
--  - add primary key in xlog
--  - add secondary key in xlog
--  - the same for an empty space and a space with data
--  - test start from a space entry added in xlog
--  - test start from a space entry dropped in xlog
--  - test enabled/disabled property for these
--  spaces and space from a snapshot
--
--
-- features
--------
-- - ffi function to enable/disable space
