dofile('utils.lua')

--=============================================================================
-- 32-bit hash tests
--=============================================================================
-------------------------------------------------------------------------------
-- 32-bit hash insert fields tests
-------------------------------------------------------------------------------

-- Insert valid fields
box.space[10]:insert(0, 'value1 v1.0', 'value2 v1.0')
box.space[10]:insert(1, 'value1 v1.0', 'value2 v1.0')
box.space[10]:insert(2, 'value1 v1.0', 'value2 v1.0')
box.space[10]:insert(3, 'value1 v1.0', 'value2 v1.0')

-- Insert invalid fields
box.space[10]:insert('invalid key', 'value1 v1.0', 'value2 v1.0')

-------------------------------------------------------------------------------
-- 32-bit hash replace fields tests
-------------------------------------------------------------------------------

-- Replace valid fields
box.space[10]:replace(3, 'value1 v1.31', 'value2 1.12')
box.space[10]:replace(1, 'value1 v1.32', 'value2 1.72')
box.space[10]:replace(2, 'value1 v1.43', 'value2 1.92')

-- Replace invalid fields
box.space[10]:replace('invalid key', 'value1 v1.0', 'value2 v1.0')

-------------------------------------------------------------------------------
-- 32-bit hash select fields test
-------------------------------------------------------------------------------

-- select by valid keys
box.space[10]:select(0, 0)
box.space[10]:select(0, 1)
box.space[10]:select(0, 2)
box.space[10]:select(0, 3)
box.space[10]:select(0, 4)
box.space[10]:select(0, 5)

-- select by invalid keys
box.space[10]:select(0, 'invalid key')
box.space[10]:select(0, 1, 2)

-------------------------------------------------------------------------------
-- 32-bit hash delete fields test
-------------------------------------------------------------------------------

-- delete by valid keys
box.space[10]:delete(0)
box.space[10]:delete(1)
box.space[10]:delete(2)
box.space[10]:delete(3)
box.space[10]:delete(4)
box.space[10]:delete(5)

-- delete by invalid keys
box.space[10]:delete('invalid key')
box.space[10]:delete(1, 2)

--=============================================================================
-- 64-bit hash tests
--=============================================================================
-------------------------------------------------------------------------------
-- 64-bit hash inset fields tests
-------------------------------------------------------------------------------

-- Insert valid fields
box.space[11]:insert(0ULL, 'value1 v1.0', 'value2 v1.0')
box.space[11]:insert(1ULL, 'value1 v1.0', 'value2 v1.0')
box.space[11]:insert(2ULL, 'value1 v1.0', 'value2 v1.0')
box.space[11]:insert(3ULL, 'value1 v1.0', 'value2 v1.0')

-- Insert invalid fields
box.space[11]:insert(100, 'value1 v1.0', 'value2 v1.0')
box.space[11]:insert(101, 'value1 v1.0', 'value2 v1.0')
box.space[11]:insert(102, 'value1 v1.0', 'value2 v1.0')
box.space[11]:insert(103, 'value1 v1.0', 'value2 v1.0')
box.space[11]:insert('invalid key', 'value1 v1.0', 'value2 v1.0')

-------------------------------------------------------------------------------
-- 64-bit hash replace fields tests
-------------------------------------------------------------------------------

-- Replace valid fields
box.space[11]:replace(3ULL, 'value1 v1.31', 'value2 1.12')
box.space[11]:replace(1ULL, 'value1 v1.32', 'value2 1.72')
box.space[11]:replace(2ULL, 'value1 v1.43', 'value2 1.92')

-- Replace invalid fields
box.space[11]:replace(3, 'value1 v1.31', 'value2 1.12')
box.space[11]:replace(1, 'value1 v1.32', 'value2 1.72')
box.space[11]:replace(2, 'value1 v1.43', 'value2 1.92')
box.space[11]:replace('invalid key', 'value1 v1.0', 'value2 v1.0')

-------------------------------------------------------------------------------
-- 64-bit hash select fields test
-------------------------------------------------------------------------------

-- select by valid keys
box.space[11]:select(0, 0ULL)
box.space[11]:select(0, 1ULL)
box.space[11]:select(0, 2ULL)
box.space[11]:select(0, 3ULL)
box.space[11]:select(0, 4ULL)
box.space[11]:select(0, 5ULL)

-- select by valid NUM keys
box.space[11]:select(0, 0)
box.space[11]:select(0, 1)
box.space[11]:select(0, 2)
box.space[11]:select(0, 3)
box.space[11]:select(0, 4)
box.space[11]:select(0, 5)

-- select by invalid keys
box.space[11]:select(0, 'invalid key')
box.space[11]:select(0, '00000001', '00000002')

-------------------------------------------------------------------------------
-- 64-bit hash delete fields test
-------------------------------------------------------------------------------

-- delete by valid keys
box.space[11]:delete(0ULL)
box.space[11]:delete(1ULL)
box.space[11]:delete(2ULL)
box.space[11]:delete(3ULL)
box.space[11]:delete(4ULL)
box.space[11]:delete(5ULL)

box.space[11]:insert(0ULL, 'value1 v1.0', 'value2 v1.0')
box.space[11]:insert(1ULL, 'value1 v1.0', 'value2 v1.0')
box.space[11]:insert(2ULL, 'value1 v1.0', 'value2 v1.0')
box.space[11]:insert(3ULL, 'value1 v1.0', 'value2 v1.0')

-- delete by valid NUM keys
box.space[11]:delete(0)
box.space[11]:delete(1)
box.space[11]:delete(2)
box.space[11]:delete(3)
box.space[11]:delete(4)
box.space[11]:delete(5)

-- delete by invalid keys
box.space[11]:delete('invalid key')
box.space[11]:delete('00000001', '00000002')

--=============================================================================
-- String hash tests
--=============================================================================
-------------------------------------------------------------------------------
-- String hash inset fields tests
-------------------------------------------------------------------------------

-- Insert valid fields
box.space[12]:insert('key 0', 'value1 v1.0', 'value2 v1.0')
box.space[12]:insert('key 1', 'value1 v1.0', 'value2 v1.0')
box.space[12]:insert('key 2', 'value1 v1.0', 'value2 v1.0')
box.space[12]:insert('key 3', 'value1 v1.0', 'value2 v1.0')

-------------------------------------------------------------------------------
-- String hash replace fields tests
-------------------------------------------------------------------------------

-- Replace valid fields
box.space[12]:replace('key 3', 'value1 v1.31', 'value2 1.12')
box.space[12]:replace('key 1', 'value1 v1.32', 'value2 1.72')
box.space[12]:replace('key 2', 'value1 v1.43', 'value2 1.92')

-------------------------------------------------------------------------------
-- String hash select fields test
-------------------------------------------------------------------------------

-- select by valid keys
box.space[12]:select(0, 'key 0')
box.space[12]:select(0, 'key 1')
box.space[12]:select(0, 'key 2')
box.space[12]:select(0, 'key 3')
box.space[12]:select(0, 'key 4')
box.space[12]:select(0, 'key 5')

-- select by invalid keys
box.space[12]:select(0, 'key 1', 'key 2')

-------------------------------------------------------------------------------
-- String hash delete fields test
-------------------------------------------------------------------------------

-- delete by valid keys
box.space[12]:delete('key 0')
box.space[12]:delete('key 1')
box.space[12]:delete('key 2')
box.space[12]:delete('key 3')
box.space[12]:delete('key 4')
box.space[12]:delete('key 5')

-- delete by invalid keys
box.space[12]:delete('key 1', 'key 2')

-- clean-up
box.space[10]:truncate()
box.space[11]:truncate()
box.space[12]:truncate()

------------------------
-- hash::replace tests
------------------------

box.space[21]:truncate()
box.space[21]:insert(0, 0, 0, 0)
box.space[21]:insert(1, 1, 1, 1)
box.space[21]:insert(2, 2, 2, 2)

-- OK
box.replace_if_exists(21, 1, 1, 1, 1)
box.replace_if_exists(21, 1, 10, 10, 10)
box.replace_if_exists(21, 1, 1, 1, 1)
box.space[21]:select(0, 10)
box.space[21]:select(1, 10)
box.space[21]:select(2, 10)
box.space[21]:select(3, 10)
box.space[21]:select(0, 1)
box.space[21]:select(1, 1)
box.space[21]:select(2, 1)
box.space[21]:select(3, 1)

-- OK
box.space[21]:insert(10, 10, 10, 10)
box.space[21]:delete(10)
box.space[21]:select(0, 10)
box.space[21]:select(1, 10)
box.space[21]:select(2, 10)
box.space[21]:select(3, 10)

-- TupleFound (primary key)
box.space[21]:insert(1, 10, 10, 10)
box.space[21]:select(0, 10)
box.space[21]:select(1, 10)
box.space[21]:select(2, 10)
box.space[21]:select(3, 10)
box.space[21]:select(0, 1)

-- TupleNotFound (primary key)
box.replace_if_exists(21, 10, 10, 10, 10)
box.space[21]:select(0, 10)
box.space[21]:select(1, 10)
box.space[21]:select(2, 10)
box.space[21]:select(3, 10)

-- TupleFound (key --1)
box.space[21]:insert(10, 0, 10, 10)
box.space[21]:select(0, 10)
box.space[21]:select(1, 10)
box.space[21]:select(2, 10)
box.space[21]:select(3, 10)
box.space[21]:select(1, 0)

-- TupleFound (key --1)
box.replace_if_exists(21, 2, 0, 10, 10)
box.space[21]:select(0, 10)
box.space[21]:select(1, 10)
box.space[21]:select(2, 10)
box.space[21]:select(3, 10)
box.space[21]:select(1, 0)

-- TupleFound (key --3)
box.space[21]:insert(10, 10, 10, 0)
box.space[21]:select(0, 10)
box.space[21]:select(1, 10)
box.space[21]:select(2, 10)
box.space[21]:select(3, 10)
box.space[21]:select(3, 0)

-- TupleFound (key --3)
box.replace_if_exists(21, 2, 10, 10, 0)
box.space[21]:select(0, 10)
box.space[21]:select(1, 10)
box.space[21]:select(2, 10)
box.space[21]:select(3, 10)
box.space[21]:select(3, 0)
box.space[21]:truncate()

-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
