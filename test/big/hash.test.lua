dofile('utils.lua')

--=============================================================================
-- 32-bit hash tests
--=============================================================================
-------------------------------------------------------------------------------
-- 32-bit hash insert fields tests
-------------------------------------------------------------------------------

box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')

hash = box.space[0]

-- Insert valid fields
hash:insert(0, 'value1 v1.0', 'value2 v1.0')
hash:insert(1, 'value1 v1.0', 'value2 v1.0')
hash:insert(2, 'value1 v1.0', 'value2 v1.0')
hash:insert(3, 'value1 v1.0', 'value2 v1.0')

-- Insert invalid fields
hash:insert('invalid key', 'value1 v1.0', 'value2 v1.0')

-------------------------------------------------------------------------------
-- 32-bit hash replace fields tests
-------------------------------------------------------------------------------

-- Replace valid fields
hash:replace(3, 'value1 v1.31', 'value2 1.12')
hash:replace(1, 'value1 v1.32', 'value2 1.72')
hash:replace(2, 'value1 v1.43', 'value2 1.92')

-- Replace invalid fields
hash:replace('invalid key', 'value1 v1.0', 'value2 v1.0')

-------------------------------------------------------------------------------
-- 32-bit hash select fields test
-------------------------------------------------------------------------------

-- select by valid keys
hash:select(0, 0)
hash:select(0, 1)
hash:select(0, 2)
hash:select(0, 3)
hash:select(0, 4)
hash:select(0, 5)

-- select by invalid keys
hash:select(0, 'invalid key')
hash:select(0, 1, 2)

-------------------------------------------------------------------------------
-- 32-bit hash delete fields test
-------------------------------------------------------------------------------

-- delete by valid keys
hash:delete(0)
hash:delete(1)
hash:delete(2)
hash:delete(3)
hash:delete(4)
hash:delete(5)

-- delete by invalid keys
hash:delete('invalid key')
hash:delete(1, 2)

hash:truncate()

--=============================================================================
-- 64-bit hash tests
--=============================================================================
-------------------------------------------------------------------------------
-- 64-bit hash inset fields tests
-------------------------------------------------------------------------------
box.replace(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num64')
hash = box.space[0]

-- Insert valid fields
hash:insert(0ULL, 'value1 v1.0', 'value2 v1.0')
hash:insert(1ULL, 'value1 v1.0', 'value2 v1.0')
hash:insert(2ULL, 'value1 v1.0', 'value2 v1.0')
hash:insert(3ULL, 'value1 v1.0', 'value2 v1.0')

-- Insert invalid fields
hash:insert(100, 'value1 v1.0', 'value2 v1.0')
hash:insert(101, 'value1 v1.0', 'value2 v1.0')
hash:insert(102, 'value1 v1.0', 'value2 v1.0')
hash:insert(103, 'value1 v1.0', 'value2 v1.0')
hash:insert('invalid key', 'value1 v1.0', 'value2 v1.0')

-------------------------------------------------------------------------------
-- 64-bit hash replace fields tests
-------------------------------------------------------------------------------

-- Replace valid fields
hash:replace(3ULL, 'value1 v1.31', 'value2 1.12')
hash:replace(1ULL, 'value1 v1.32', 'value2 1.72')
hash:replace(2ULL, 'value1 v1.43', 'value2 1.92')

-- Replace invalid fields
hash:replace(3, 'value1 v1.31', 'value2 1.12')
hash:replace(1, 'value1 v1.32', 'value2 1.72')
hash:replace(2, 'value1 v1.43', 'value2 1.92')
hash:replace('invalid key', 'value1 v1.0', 'value2 v1.0')

-------------------------------------------------------------------------------
-- 64-bit hash select fields test
-------------------------------------------------------------------------------

-- select by valid keys
hash:select(0, 0ULL)
hash:select(0, 1ULL)
hash:select(0, 2ULL)
hash:select(0, 3ULL)
hash:select(0, 4ULL)
hash:select(0, 5ULL)

-- select by valid NUM keys
hash:select(0, 0)
hash:select(0, 1)
hash:select(0, 2)
hash:select(0, 3)
hash:select(0, 4)
hash:select(0, 5)

-- select by invalid keys
hash:select(0, 'invalid key')
hash:select(0, '00000001', '00000002')

-------------------------------------------------------------------------------
-- 64-bit hash delete fields test
-------------------------------------------------------------------------------

-- delete by valid keys
hash:delete(0ULL)
hash:delete(1ULL)
hash:delete(2ULL)
hash:delete(3ULL)
hash:delete(4ULL)
hash:delete(5ULL)

hash:insert(0ULL, 'value1 v1.0', 'value2 v1.0')
hash:insert(1ULL, 'value1 v1.0', 'value2 v1.0')
hash:insert(2ULL, 'value1 v1.0', 'value2 v1.0')
hash:insert(3ULL, 'value1 v1.0', 'value2 v1.0')

-- delete by valid NUM keys
hash:delete(0)
hash:delete(1)
hash:delete(2)
hash:delete(3)
hash:delete(4)
hash:delete(5)

-- delete by invalid keys
hash:delete('invalid key')
hash:delete('00000001', '00000002')
hash:truncate()

--=============================================================================
-- String hash tests
--=============================================================================
-------------------------------------------------------------------------------
-- String hash inset fields tests
-------------------------------------------------------------------------------
box.replace(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'str')
hash = box.space[0]

-- Insert valid fields
hash:insert('key 0', 'value1 v1.0', 'value2 v1.0')
hash:insert('key 1', 'value1 v1.0', 'value2 v1.0')
hash:insert('key 2', 'value1 v1.0', 'value2 v1.0')
hash:insert('key 3', 'value1 v1.0', 'value2 v1.0')

-------------------------------------------------------------------------------
-- String hash replace fields tests
-------------------------------------------------------------------------------

-- Replace valid fields
hash:replace('key 3', 'value1 v1.31', 'value2 1.12')
hash:replace('key 1', 'value1 v1.32', 'value2 1.72')
hash:replace('key 2', 'value1 v1.43', 'value2 1.92')

-------------------------------------------------------------------------------
-- String hash select fields test
-------------------------------------------------------------------------------

-- select by valid keys
hash:select(0, 'key 0')
hash:select(0, 'key 1')
hash:select(0, 'key 2')
hash:select(0, 'key 3')
hash:select(0, 'key 4')
hash:select(0, 'key 5')

-- select by invalid keys
hash:select(0, 'key 1', 'key 2')

-------------------------------------------------------------------------------
-- String hash delete fields test
-------------------------------------------------------------------------------

-- delete by valid keys
hash:delete('key 0')
hash:delete('key 1')
hash:delete('key 2')
hash:delete('key 3')
hash:delete('key 4')
hash:delete('key 5')

-- delete by invalid keys
hash:delete('key 1', 'key 2')
hash:truncate()

------------------------
-- hash::replace tests
------------------------
box.replace(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')
box.replace(box.schema.INDEX_ID, 0, 1, 'field1', 'hash', 1, 1, 1, 'num')
box.replace(box.schema.INDEX_ID, 0, 2, 'field2', 'hash', 1, 1, 2, 'num')
box.replace(box.schema.INDEX_ID, 0, 3, 'field3', 'hash', 1, 1, 3, 'num')
hash = box.space[0]

hash:insert(0, 0, 0, 0)
hash:insert(1, 1, 1, 1)
hash:insert(2, 2, 2, 2)

-- OK
hash:replace_if_exists(1, 1, 1, 1)
hash:replace_if_exists(1, 10, 10, 10)
hash:replace_if_exists(1, 1, 1, 1)
hash:select(0, 10)
hash:select(1, 10)
hash:select(2, 10)
hash:select(3, 10)
hash:select(0, 1)
hash:select(1, 1)
hash:select(2, 1)
hash:select(3, 1)

-- OK
hash:insert(10, 10, 10, 10)
hash:delete(10)
hash:select(0, 10)
hash:select(1, 10)
hash:select(2, 10)
hash:select(3, 10)

-- TupleFound (primary key)
hash:insert(1, 10, 10, 10)
hash:select(0, 10)
hash:select(1, 10)
hash:select(2, 10)
hash:select(3, 10)
hash:select(0, 1)

-- TupleNotFound (primary key)
hash:replace_if_exists(10, 10, 10, 10)
hash:select(0, 10)
hash:select(1, 10)
hash:select(2, 10)
hash:select(3, 10)

-- TupleFound (key --1)
hash:insert(10, 0, 10, 10)
hash:select(0, 10)
hash:select(1, 10)
hash:select(2, 10)
hash:select(3, 10)
hash:select(1, 0)

-- TupleFound (key --1)
hash:replace_if_exists(2, 0, 10, 10)
hash:select(0, 10)
hash:select(1, 10)
hash:select(2, 10)
hash:select(3, 10)
hash:select(1, 0)

-- TupleFound (key --3)
hash:insert(10, 10, 10, 0)
hash:select(0, 10)
hash:select(1, 10)
hash:select(2, 10)
hash:select(3, 10)
hash:select(3, 0)

-- TupleFound (key --3)
hash:replace_if_exists(2, 10, 10, 0)
hash:select(0, 10)
hash:select(1, 10)
hash:select(2, 10)
hash:select(3, 10)
hash:select(3, 0)
hash:drop()
