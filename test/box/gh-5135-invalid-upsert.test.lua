--
-- gh-5135: there was a crash in case upsert operation was
-- invalid with an error appeared not in the operation, but in
-- something else. For example, bad json. Or json type mismatching
-- field type (attempt to do [...] on a scalar field). The
-- expected behaviour is that such operations are just skipped.
-- All the crashes were related to a so called 'bar' update. When
-- an operation has a json path not interleaving with any other
-- path.
-- In all tests no crash should happen + not less importantly the
-- bad operation should be nop, and should not affect other
-- operations.
--

ops = {}
ops[1] = {'!', 1, 1}
ops[3] = {'!', 3, 2}

-- Duplicate in a map.
t = box.tuple.new({{a = 100}})
ops[2] = {'!', '[1].a', 200}
t:upsert(ops)

-- Bad JSON when do '!'.
ops[2] = {'!', '[1].a[crash]', 200}
t:upsert(ops)

-- Bad JSON when do '='.
t = box.tuple.new({{1}})
ops[2] = {'!', '[1][crash]', 200}
t:upsert(ops)

-- Can't delete more than 1 field from map in one
-- operation.
t = box.tuple.new({{a = 100}})
ops[2] = {'#', '[1].a', 2}
t:upsert(ops)

-- Bad JSON in '#'
ops[2] = {'#', '[1].a[crash]', 1}
t:upsert(ops)

-- Bad JSON in scalar operations.
ops[2] = {'+', '[1].a[crash]', 1}
t:upsert(ops)
t = box.tuple.new({{1}})
ops[2] = {'&', '[1][crash]', 2}
t:upsert(ops)

-- Several fields, multiple operations, path
-- interleaving.
t = box.tuple.new({{1}, {2}})
t:upsert({{'+', '[2][1]', 1}, {'&', '[1][crash]', 2}, {'=', '[3]', {4}}})

t = box.tuple.new({ { { { 1 } }, { {a = 2} } }, { 3 } })
t:upsert({{'=', '[1][1][1]', 4}, {'!', '[1][2][1].a', 5}, {'-', '[2][1]', 4}})

-- A valid operation on top of invalid, for the
-- same field.
t:upsert({{'+', '[1][1][1].a', 10}, {'+', '[1][1][1]', -10}})

-- Invalid operand of an arith operation. Also should turn into
-- nop.
t:upsert({{'+', '[1][1][1]', 10}})
-- This should be correct.
t:upsert({{'+', '[1][1][1][1]', 10}})

-- Check that invalid insertion can't screw indexes. I.e. next
-- operations won't see the not added field.
t = box.tuple.new({{1, 2, 3}})
-- The second operation should change 2 to 12, not 20 to 30.
t:upsert({{'!', '[1][2][100]', 20}, {'+', '[1][2]', 10}})
