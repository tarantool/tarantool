local avl = require('utils.avl')
local tap = require('tap')

local test = tap.test('tools-utils-avl')
test:plan(7)

local function traverse(node, result)
  result = result or { }
  if node ~= nil then
    table.insert(result, node.key)
    traverse(node.left, result)
    traverse(node.right, result)
  end
  return result
end

local function batch_insert(values, root)
  for i = 1, #values do
    root = avl.insert(root, values[i])
  end
  return root
end

-- 1L rotation test.
test:is_deeply(traverse(batch_insert({1, 2, 3})), {2, 1, 3})
-- 1R rotation test.
test:is_deeply(traverse(batch_insert({3, 2, 1})), {2, 1, 3})
-- 2L rotation test.
test:is_deeply(traverse(batch_insert({1, 3, 2})), {2, 1, 3})
-- 2R rotation test.
test:is_deeply(traverse(batch_insert({3, 1, 2})), {2, 1, 3})

local root = batch_insert({1, 2, 3})
-- Exact upper bound.
test:is(avl.floor(root, 1), 1)
-- No upper bound.
test:is(avl.floor(root, -10), nil)
-- Not exact upper bound.
test:is(avl.floor(root, 2.75), 2)

test:done(true)
