-- The benchmark to check the performance of the GC and memory
-- allocator. Allocate, walk, and deallocate many bottom-up binary
-- trees.
-- For the details, see:
-- https://benchmarksgame-team.pages.debian.net/benchmarksgame/description/binarytrees.html

local bench = require("bench").new(arg)

local function BottomUpTree(item, depth)
  if depth > 0 then
    local i = item + item
    depth = depth - 1
    local left, right = BottomUpTree(i-1, depth), BottomUpTree(i, depth)
    return { item, left, right }
  else
    return { item }
  end
end

-- The checker function. For the tree created with the given
-- `item` returns `item` - 1 (by induction).
local function ItemCheck(tree)
  if tree[2] then
    return tree[1] + ItemCheck(tree[2]) - ItemCheck(tree[3])
  else
    return tree[1]
  end
end

local N = tonumber(arg and arg[1]) or 16
local mindepth = 4
local maxdepth = mindepth + 2
if maxdepth < N then maxdepth = N end

local stretchdepth = maxdepth + 1

-- Allocate a binary tree to "stretch" memory, check it exists,
-- and "deallocate" it.
bench:add({
  name = "stretch_depth_" .. tostring(stretchdepth),
  payload = function()
    local stretchtree = BottomUpTree(0, stretchdepth)
    local check = ItemCheck(stretchtree)
    return check
  end,
  items = 1,
  checker = function(check)
    return check == -1
  end,
})

-- Allocate a long-lived binary tree that will live on while
-- other trees are allocated and "deallocated".
-- This tree created once on the setup for the first test.
local longlivedtree

-- Allocate, walk, and "deallocate" many bottom-up binary trees.
for depth = mindepth, maxdepth, 2 do
  local iterations = 2 ^ (maxdepth - depth + mindepth)
  local tree_bench
  tree_bench = {
    name = "tree_depth_" .. tostring(depth),
    setup = function()
      if not longlivedtree then
        longlivedtree = BottomUpTree(0, maxdepth)
      end
      tree_bench.items = iterations * 2
    end,
    checker = function(check)
      return check == -iterations * 2
    end,
    payload = function()
      local check = 0
      for i = 1, iterations do
        check = check + ItemCheck(BottomUpTree(1, depth)) +
                ItemCheck(BottomUpTree(-1, depth))
      end
      return check
    end,
  }

  bench:add(tree_bench)
end

-- Check that the long-lived binary tree still exists.
bench:add({
  name = "longlived_depth_" .. tostring(maxdepth),
  payload = function()
    local check = ItemCheck(longlivedtree)
    return check
  end,
  items = 1,
  checker = function(check)
    return check == -1
  end,
})

-- All in one benchmark for the various trees.
bench:add({
  name = "all_in_one",
  payload = function()
    for depth = mindepth, maxdepth, 2 do
      local iterations = 2 ^ (maxdepth - depth + mindepth)
      local tree_bench
      local check = 0
      for i = 1, iterations do
        check = check + ItemCheck(BottomUpTree(1, depth)) +
                ItemCheck(BottomUpTree(-1, depth))
      end
      assert(check == -iterations * 2)
    end
  end,
  -- Geometric progression, starting at maxdepth trees with the
  -- corresponding step.
  items = (2 * maxdepth) * (4 ^ ((maxdepth - mindepth) / 2 + 1) - 1) / 3,
  -- Correctness is checked in the payload function.
  skip_check = true,
})

bench:run_and_report()
