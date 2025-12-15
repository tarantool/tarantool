local M = {}
local max = math.max

local function create_node(key, value)
  return {
    key = key,
    value = value,
    left = nil,
    right = nil,
    height = 1,
  }
end

local function height(node)
  if node == nil then
    return 0
  end
  return node.height
end

local function update_height(node)
  node.height = 1 + max(height(node.left), height(node.right))
end

local function get_balance(node)
  if node == nil then
    return 0
  end
  return height(node.left) - height(node.right)
end

local function rotate_left(node)
    local r_subtree = node.right;
    local rl_subtree = r_subtree.left;

    r_subtree.left = node;
    node.right = rl_subtree;

    update_height(node)
    update_height(r_subtree)

    return r_subtree;
end

local function rotate_right(node)
    local l_subtree = node.left
    local lr_subtree = l_subtree.right;

    l_subtree.right = node;
    node.left = lr_subtree;

    update_height(node)
    update_height(l_subtree)

    return l_subtree;
end

local function rebalance(node, key)
  local balance = get_balance(node)

  if -1 <= balance and balance <=1 then
    return node
  end

  if balance > 1 and key < node.left.key then
    return rotate_right(node)
  elseif balance < -1 and key > node.right.key then
    return rotate_left(node)
  elseif balance > 1 and key > node.left.key then
    node.left = rotate_left(node.left)
    return rotate_right(node)
  elseif balance < -1 and key < node.right.key then
    node.right = rotate_right(node.right)
    return rotate_left(node)
  end
end

function M.insert(node, key, value)
  assert(key, "Key can't be nil")
  if node == nil then
    return create_node(key, value)
  end

  if key < node.key then
    node.left = M.insert(node.left, key, value)
  elseif key > node.key then
    node.right = M.insert(node.right, key, value)
  else
    node.value = value
  end

  update_height(node)
  return rebalance(node, key)
end

function M.floor(node, key)
  if node == nil then
    return nil, nil
  end
  -- Explicit match.
  if key == node.key then
    return node.key, node.value
  elseif key < node.key then
    return M.floor(node.left, key)
  elseif key > node.key then
    local right_key, value = M.floor(node.right, key)
    right_key = right_key or node.key
    value = value or node.value
    return right_key, value
  end
end

return M
